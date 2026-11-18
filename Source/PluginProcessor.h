#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class PVoxProcessor : public juce::AudioProcessor
{
public:
    PVoxProcessor();
    ~PVoxProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "P-Vox"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Curvas estáticas (también usadas por la UI/auto-makeup)
    static float expanderGainDb (float levelDb, float thresholdDb);
    static float compGrDb (float overDb);
    static float makeupDb (float compThresholdDb);

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> grMeter { 0.f }, outL { 0.f }, outR { 0.f };   // GR en dB (>=0), salida lineal

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float coef (float ms) const { return std::exp (-1.0f / (0.001f * ms * (float) sr)); }

    double sr = 44100.0;
    std::atomic<float> *pGate = nullptr, *pComp = nullptr, *pGain = nullptr;

    // coeficientes
    float gDetRel = 0, gAtt = 0, gRel150 = 0, gRel250 = 0;
    float cPeakRel = 0, cRmsC = 0, cAtt = 0, cRelFast = 0, cRelSlow = 0, cAvgC = 0, lRel = 0, meterDecay = 0;
    // estados
    float gEnv = 0, gGr = 0, cPeak = 0, cRms = 0, cGr = 0, cAvg = 0, lEnv = 1.f, lastGain = -1.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PVoxProcessor)
};
