#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static inline float toDb (float x)   { return 20.0f * std::log10 (x + 1.0e-9f); }
static inline float dbToLin (float d) { return std::exp (d * 0.11512925f); }

AudioProcessorValueTreeState::ParameterLayout PVoxProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "gate", 1 }, "Gate",  NormalisableRange<float> (-60.f, 0.f, 0.1f),  -60.f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "comp", 1 }, "Comp",  NormalisableRange<float> (-36.f, 0.f, 0.1f),  0.f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain",  NormalisableRange<float> (-36.f, 12.f, 0.1f), 0.f));
    return l;
}

PVoxProcessor::PVoxProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PVOX", createLayout())
{
    pGate = apvts.getRawParameterValue ("gate");
    pComp = apvts.getRawParameterValue ("comp");
    pGain = apvts.getRawParameterValue ("gain");
}

bool PVoxProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto o = l.getMainOutputChannelSet();
    return (o == AudioChannelSet::mono() || o == AudioChannelSet::stereo()) && o == l.getMainInputChannelSet();
}

// Expansor hacia abajo, soft-knee W=6 dB, ratio 2.5:1, piso -40 dB.
// d = T - L. d<-W/2: 0 | |d|<=W/2: -(R-1)(d+W/2)^2/(2W) | d>W/2: -(R-1)d
float PVoxProcessor::expanderGainDb (float L, float T)
{
    const float W = 6.f, R = 2.5f, d = T - L;
    if (2.f * d < -W) return 0.f;
    const float g = (2.f * d > W) ? -(R - 1.f) * d : -(R - 1.f) * (d + W * 0.5f) * (d + W * 0.5f) / (2.f * W);
    return jmax (g, -40.f);
}

// Compresor: knee W=12 dB, ratio dinámico R(o)=2+4*s(x), x=clamp(o/18), s=x^2(3-2x) -> 2:1 ... 6:1
// GR = s_c*o^... : arriba del knee GR=(1-1/R)*o ; dentro: (1-1/R)(o+W/2)^2/(2W)
float PVoxProcessor::compGrDb (float o)
{
    const float W = 12.f;
    if (2.f * o < -W) return 0.f;
    const float x = jlimit (0.f, 1.f, o / 18.f);
    const float R = 2.f + 4.f * x * x * (3.f - 2.f * x);
    const float s = 1.f - 1.f / R;
    if (2.f * o > W) return s * o;
    const float k = o + W * 0.5f;
    return s * k * k / (2.f * W);
}

// Auto-makeup: 80 % de la reducción que sufre un nivel de referencia de -10 dBFS (hasta ~17 dB)
float PVoxProcessor::makeupDb (float compT)
{
    return 0.8f * compGrDb (-10.f - compT);
}

void PVoxProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    gDetRel = coef (30.f);  gAtt = coef (1.f);  gRel150 = coef (150.f);  gRel250 = coef (250.f);
    cPeakRel = coef (60.f); cRmsC = 1.f - coef (10.f); cAtt = coef (7.f);
    cRelFast = coef (50.f); cRelSlow = coef (300.f);   cAvgC = 1.f - coef (400.f);
    lRel = coef (60.f); meterDecay = coef (200.f);
    gEnv = gGr = cPeak = cRms = cGr = cAvg = 0.f;
    lEnv = 1.f; lastGain = -1.f;
    grMeter = outL = outR = 0.f;
}

void PVoxProcessor::processBlock (AudioBuffer<float>& buf, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buf.clear (c, 0, n);
    if (buf.getNumChannels() == 0 || n == 0) return;

    float* d0 = buf.getWritePointer (0);
    float* d1 = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : nullptr;

    const float gateT = pGate->load(), compT = pComp->load(), gainDb = pGain->load();
    const bool gateOn = gateT > -59.5f, compOn = compT < -0.1f;
    const float makeup = compOn ? makeupDb (compT) : 0.f;
    const float g1 = dbToLin (makeup + gainDb);
    if (lastGain < 0.f) lastGain = g1;
    const float g0 = lastGain, step = (g1 - g0) / (float) n;
    lastGain = g1;

    constexpr float ceilLin = 0.98852f;                       // -0.1 dBFS
    float grMax = 0.f, pkL = 0.f, pkR = 0.f;

    for (int i = 0; i < n; ++i)
    {
        float l = d0[i], r = d1 ? d1[i] : l;

        // ---- Etapa 1: Gate / expansor suave ----
        if (gateOn)
        {
            const float pk = jmax (std::abs (l), std::abs (r));
            gEnv = pk > gEnv ? pk : pk + (gEnv - pk) * gDetRel;
            const float tgt = expanderGainDb (toDb (gEnv), gateT);
            float c;
            if (tgt > gGr) c = gAtt;                                         // abre: 1 ms
            else           c = gRel150 + (gRel250 - gRel150) * jmin (1.f, -gGr * 0.05f);   // cierra: 150-250 ms
            gGr = tgt + (gGr - tgt) * c;
            const float gg = dbToLin (gGr);
            l *= gg; r *= gg;
        }
        else gGr = 0.f;

        // ---- Etapa 2: compresor híbrido peak/RMS, ratio y release dependientes del programa ----
        {
            const float pk = jmax (std::abs (l), std::abs (r));
            cPeak = pk > cPeak ? pk : pk + (cPeak - pk) * cPeakRel;
            cRms += (0.5f * (l * l + r * r) - cRms) * cRmsC;
            const float det = 0.5f * (cPeak + std::sqrt (cRms));
            const float tgt = compOn ? compGrDb (toDb (det) - compT) : 0.f;   // GR en dB (>=0)
            float c;
            if (tgt > cGr) c = cAtt;                                          // ataque 7 ms
            else { const float k = jlimit (0.f, 1.f, (cGr - cAvg) * 0.25f);   // transitorio -> 50 ms, sostenido -> 300 ms
                   c = cRelSlow + (cRelFast - cRelSlow) * k; }
            cGr = tgt + (cGr - tgt) * c;
            cAvg += (cGr - cAvg) * cAvgC;
            const float cg = dbToLin (-cGr) * (g0 + step * (float) i);       // GR + (auto-makeup + gain) con rampa
            l *= cg; r *= cg;
            grMax = jmax (grMax, cGr);
        }

        // ---- Etapa 3: limitador brickwall -0.1 dBFS ----
        {
            const float pk = jmax (std::abs (l), std::abs (r));
            const float need = pk > ceilLin ? ceilLin / pk : 1.f;
            lEnv = need < lEnv ? need : need + (lEnv - need) * lRel;
            l = jlimit (-ceilLin, ceilLin, l * lEnv);
            r = jlimit (-ceilLin, ceilLin, r * lEnv);
        }

        d0[i] = l; if (d1) d1[i] = r;
        pkL = jmax (pkL, std::abs (l)); pkR = jmax (pkR, std::abs (r));
    }

    const float dec = std::pow (meterDecay, (float) n);
    grMeter.store (jmax (grMax, grMeter.load() * dec));
    outL.store (jmax (pkL, outL.load() * dec));
    outR.store (jmax (d1 ? pkR : pkL, outR.load() * dec));
}

AudioProcessorEditor* PVoxProcessor::createEditor() { return new PVoxEditor (*this); }

void PVoxProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PVoxProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PVoxProcessor(); }
