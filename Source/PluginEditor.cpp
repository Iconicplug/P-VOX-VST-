#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

static const Colour kRed (0xffff0a0a), kDarkRed (0xff6b0a11);

PVoxEditor::PVoxEditor (PVoxProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    bg = ImageCache::getFromMemory (BinaryData::PVOX_bg_png, BinaryData::PVOX_bg_pngSize);
    // coordenadas en el espacio de la imagen original (1536 x 1024)
    faders[0] = { p.apvts.getParameter ("gate"), { 245, 175, 150, 700 },  245.f, 800.f };   // arriba = -60 (off), abajo = 0 dB
    faders[1] = { p.apvts.getParameter ("comp"), { 520, 230, 500, 610 },  798.f, 263.f };   // arriba = 0 dB, abajo = -36 dB
    faders[2] = { p.apvts.getParameter ("gain"), { 1175, 175, 125, 700 }, 800.f, 243.f };   // abajo = -36, arriba = +12
    setResizable (true, true);
    setResizeLimits (480, 320, 1536, 1024);
    getConstrainer()->setFixedAspectRatio (1.5);
    setSize (768, 512);
    startTimerHz (30);
}

void PVoxEditor::timerCallback()
{
    grDisp += (proc.grMeter.load() - grDisp) * 0.5f;
    lDisp = proc.outL.load(); rDisp = proc.outR.load();
    repaint();
}

void PVoxEditor::setFromY (Fader& f, float y)
{
    f.p->setValueNotifyingHost (jlimit (0.f, 1.f, (y - f.y0) / (f.y1 - f.y0)));
}

void PVoxEditor::mouseDown (const MouseEvent& e)
{
    const auto pos = e.position / scale();
    active = -1;
    for (int i = 0; i < 3; ++i)
        if (faders[i].hit.contains (pos)) { active = i; break; }
    if (active < 0) return;
    auto& f = faders[active];
    const float ty = thumbY (f);
    grab = std::abs (pos.y - ty) < 60.f ? pos.y - ty : 0.f;
    f.p->beginChangeGesture();
    setFromY (f, pos.y - grab);
}

void PVoxEditor::mouseDrag (const MouseEvent& e)
{
    if (active >= 0) setFromY (faders[active], e.position.y / scale() - grab);
}

void PVoxEditor::mouseUp (const MouseEvent&)
{
    if (active >= 0) { faders[active].p->endChangeGesture(); active = -1; }
}

void PVoxEditor::mouseDoubleClick (const MouseEvent& e)
{
    const auto pos = e.position / scale();
    for (auto& f : faders)
        if (f.hit.contains (pos)) { f.p->beginChangeGesture(); f.p->setValueNotifyingHost (f.p->getDefaultValue()); f.p->endChangeGesture(); }
}

void PVoxEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const auto pos = e.position / scale();
    for (auto& f : faders)
        if (f.hit.contains (pos))
        {
            const float dir = f.y1 < f.y0 ? 1.f : -1.f;
            f.p->setValueNotifyingHost (jlimit (0.f, 1.f, f.p->getValue() + dir * w.deltaY * 0.05f));
        }
}

static void drawThumb (Graphics& g, float cx, float cy)
{
    g.setColour (Colour (0xff0b0b0b)); g.fillEllipse (cx - 57, cy - 57, 114, 114);
    g.setColour (kRed);                g.drawEllipse (cx - 52, cy - 52, 104, 104, 7.f);
    g.setGradientFill (ColourGradient (Colour (0xffff3a24), cx, cy - 44, Colour (0xff6e0808), cx, cy + 44, false));
    g.fillEllipse (cx - 42, cy - 42, 84, 84);
    g.setColour (Colours::white.withAlpha (0.18f)); g.fillEllipse (cx - 28, cy - 36, 56, 26);
}

static String fmt (float v, bool plus = false)
{
    String s (v, 1);
    return (plus && v > 0.f) ? "+" + s : s;
}

void PVoxEditor::paint (Graphics& g)
{
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (bg, getLocalBounds().toFloat());
    g.addTransform (AffineTransform::scale (scale()));

    const float gate = faders[0].p->convertFrom0to1 (faders[0].p->getValue());
    const float comp = faders[1].p->convertFrom0to1 (faders[1].p->getValue());
    const float gain = faders[2].p->convertFrom0to1 (faders[2].p->getValue());

    // ---- Izquierda (Gate): relleno desde arriba hasta el fader ----
    float ty = thumbY (faders[0]);
    g.setColour (kRed);  g.fillRoundedRectangle (298, 215, 44, ty - 215, 10.f);
    drawThumb (g, 320.f, ty);

    // ---- Derecha (Gain): relleno desde abajo hasta el fader ----
    ty = thumbY (faders[2]);
    g.setColour (kRed);  g.fillRoundedRectangle (1222, ty, 28, 840 - ty, 10.f);
    drawThumb (g, 1236.f, ty);

    // ---- Medidor de salida (L/R) ----
    for (int c = 0; c < 2; ++c)
    {
        const float x = 1296.f + 14.f * (float) c, top = 190.f, h = 670.f;
        const float db = Decibels::gainToDecibels ((c == 0 ? lDisp : rDisp), -48.f);
        const float fh = h * jlimit (0.f, 1.f, (db + 48.f) / 48.f);
        g.setColour (Colour (0xff1a1a1a)); g.fillRoundedRectangle (x, top, 10, h, 4.f);
        g.setColour (db > -1.f ? Colours::white : kRed); g.fillRoundedRectangle (x, top + h - fh, 10, fh, 4.f);
    }

    // ---- Centro: medidor de reducción (0 .. -24 dB) con clip redondeado ----
    {
        Path clip; clip.addRoundedRectangle (618.f, 263.f, 305.f, 535.f, 38.f);
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (clip);
        const float grH = 535.f * jlimit (0.f, 1.f, grDisp / 24.f);
        g.setColour (kDarkRed); g.fillRect (618.f, 263.f, 305.f, grH);
        g.setColour (Colour (0x55ff5a5a));
        for (float y : { 351.f, 447.f, 529.f, 619.f, 711.f }) g.drawHorizontalLine ((int) y, 618.f, 923.f);
    }
    const float hy = thumbY (faders[1]);
    g.setColour (kRed);  g.fillRect (585.f, hy - 3.5f, 370.f, 7.f);
    g.setColour (Colour (0xff120202)); g.fillRoundedRectangle (668, hy - 48, 204, 96, 48.f);
    g.setColour (kRed);  g.drawRoundedRectangle (668, hy - 48, 204, 96, 48.f, 8.f);
    g.setColour (Colours::white); g.setFont (Font (FontOptions (60.f, Font::bold)));
    g.drawText (fmt (comp), Rectangle<float> (668, hy - 48, 204, 96), Justification::centred);

    // Lectura de GR (abajo)
    g.setFont (Font (FontOptions (56.f, Font::bold)));
    g.drawText (grDisp < 0.05f ? "0.0" : "-" + fmt (grDisp), Rectangle<float> (670, 840, 200, 70), Justification::centred);

    // ---- Números bajo los parámetros izquierdo / derecho ----
    g.setColour (Colour (0xffffd2d2)); g.setFont (Font (FontOptions (50.f, Font::bold)));
    g.drawText (gate <= -59.5f ? String ("OFF") : fmt (gate) + " dB", Rectangle<float> (190, 948, 260, 60), Justification::centred);
    g.drawText (fmt (gain, true) + " dB", Rectangle<float> (1105, 948, 260, 60), Justification::centred);
}
