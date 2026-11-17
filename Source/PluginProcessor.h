#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PitchTrack.h"

class VegaHarmonicProcessor : public juce::AudioProcessor
{
public:
    VegaHarmonicProcessor();
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vega Productions Harmonic"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    juce::AudioProcessorValueTreeState apvts;
    static constexpr int numHarm = 8;
    static float harmonicGainDb (int k, float gain, float oddEven, float tilt);   // k = 1..8

    std::atomic<float> uiF0 { 0 }, uiConf { 0 }, uiSub { 0 }, outL { 0 }, outR { 0 }, uiScale { 1.0f };

private:
    double sr = 48000;
    PitchTrack tracker; float f0s = 0, lastF0Set = -1; int unvoicedFor = 0;
    juce::dsp::IIR::Filter<float> peaks[numHarm][2];
    float lastParams[4] { -100, -100, -100, -100 };
    // sub generator
    juce::dsp::IIR::Filter<float> subLp1, subLp2, outLp1[2], outLp2[2];
    float subEnv = 0, prevX = 0; bool ff1 = false, ff2 = false; double ph1 = 0, ph2 = 0;
    float sqLp1 = 0, sqLp2 = 0;
    float lastRange = -1;
    std::atomic<float>* p[16] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VegaHarmonicProcessor)
};
