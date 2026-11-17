#pragma once
#include "PluginProcessor.h"
#include "VegaUI.h"

class VegaHarmonicEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VegaHarmonicEditor (VegaHarmonicProcessor&);
    ~VegaHarmonicEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    static constexpr int W = 1060, H = 560;
    class Main;
private:
    void timerCallback() override;
    VegaHarmonicProcessor& proc;
    VegaLookAndFeel lnf;
    std::unique_ptr<Main> main;
    juce::ComponentBoundsConstrainer constrainer;
};
