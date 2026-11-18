#pragma once
#include "PluginProcessor.h"

class PVoxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PVoxEditor (PVoxProcessor&);
    ~PVoxEditor() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Fader { juce::RangedAudioParameter* p; juce::Rectangle<float> hit; float y0, y1; };  // y0: n=0, y1: n=1
    void timerCallback() override;
    float scale() const { return (float) getWidth() / 1536.f; }
    float thumbY (const Fader& f) const { return f.y0 + (f.y1 - f.y0) * f.p->getValue(); }
    void setFromY (Fader&, float y);

    PVoxProcessor& proc;
    juce::Image bg;
    Fader faders[3];
    int active = -1;
    float grab = 0.f, grDisp = 0.f, lDisp = 0.f, rDisp = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PVoxEditor)
};
