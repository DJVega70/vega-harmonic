#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
enum { kHarmOn, kGain, kOddEven, kTilt, kWidth, kSubOn, kSubMode, kSub1, kSub2, kRange, kDrive, kSubOnly, kMix, kOut };
const char* ids[] = { "harmOn", "harmGain", "oddEven", "tilt", "width", "subOn", "subMode", "sub1", "sub2", "subRange", "subDrive", "subOnly", "mix", "output" };
constexpr int numP = 14;
}

float VegaHarmonicProcessor::harmonicGainDb (int k, float gain, float oddEven, float tilt)
{
    const bool odd = (k % 2) == 1;
    float w = 1.0f;
    if (oddEven < 0) w = odd ? 1.0f : 1.0f + oddEven;           // -1 = odd harmonics only
    else             w = odd ? 1.0f - oddEven : 1.0f;            // +1 = even harmonics only
    if (k == 1) w = std::max (w, 0.5f);                          // keep some of the fundamental either way
    const float pos = (k - 1) / (float) (numHarm - 1) * 2.0f - 1.0f;   // -1 (fundamental) .. +1 (8th)
    w *= juce::jlimit (0.0f, 2.0f, 1.0f + tilt * pos);
    return gain * w;
}

juce::AudioProcessorValueTreeState::ParameterLayout VegaHarmonicProcessor::layout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto pct = [] (float x, int) { return String (roundToInt (x)) + "%"; };
    auto F = [&] (int id, const char* name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids[id], 1 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (f))); };
    auto B = [&] (int id, const char* name, bool def) { v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[id], 1 }, name, def)); };
    B (kHarmOn, "Harmonic EQ", true);
    F (kGain, "Harmonics", NormalisableRange<float> (-18, 18), 6, [] (float x, int) { return (x > 0 ? "+" : "") + String (x, 1) + " dB"; });
    F (kOddEven, "Odd / Even", NormalisableRange<float> (-100, 100), 0, [] (float x, int) { return std::abs (x) < 1 ? String ("All") : x < 0 ? "Odd " + String (roundToInt (-x)) + "%" : "Even " + String (roundToInt (x)) + "%"; });
    F (kTilt, "Tilt", NormalisableRange<float> (-100, 100), 0, [] (float x, int) { return std::abs (x) < 1 ? String ("Flat") : x < 0 ? "Low " + String (roundToInt (-x)) : "High " + String (roundToInt (x)); });
    F (kWidth, "Focus", NormalisableRange<float> (1, 40, 0, 0.5f), 8, [] (float x, int) { return "Q " + String (x, 1); });
    B (kSubOn, "Subharmonics", true);
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kSubMode], 1 }, "Sub Mode", StringArray { "Divider (warm)", "Tracked (clean sine)" }, 1));
    F (kSub1, "Sub -1 Oct", NormalisableRange<float> (0, 100), 50, pct);
    F (kSub2, "Sub -2 Oct", NormalisableRange<float> (0, 100), 0, pct);
    F (kRange, "Sub Range", NormalisableRange<float> (40, 300, 0, 0.6f), 140, [] (float x, int) { return "< " + String (roundToInt (x)) + " Hz"; });
    F (kDrive, "Sub Drive", NormalisableRange<float> (0, 100), 10, pct);
    B (kSubOnly, "Listen to Sub", false);
    F (kMix, "Mix", NormalisableRange<float> (0, 100), 100, pct);
    F (kOut, "Output", NormalisableRange<float> (-24, 12), 0, [] (float x, int) { return String (x, 1) + " dB"; });
    return { v.begin(), v.end() };
}

VegaHarmonicProcessor::VegaHarmonicProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", layout())
{
    for (int i = 0; i < numP; ++i) p[i] = apvts.getRawParameterValue (ids[i]);
}

bool VegaHarmonicProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() && (l.getMainInputChannelSet() == o || l.getMainInputChannelSet() == juce::AudioChannelSet::mono());
}

void VegaHarmonicProcessor::prepareToPlay (double s, int)
{
    sr = s;
    tracker.prepare (s);
    for (auto& h : peaks) for (auto& f : h) { f.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (s, 1000.0, 1.0, 1.0f); f.reset(); }
    for (auto* f : { &subLp1, &subLp2, &outLp1[0], &outLp2[0], &outLp1[1], &outLp2[1] }) { f->coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (s, 140.0, 0.707f); f->reset(); }
    f0s = 0; lastF0Set = -1; unvoicedFor = 1000; lastRange = -1; subEnv = 0; ph1 = ph2 = 0;
    for (auto& v : lastParams) v = -100;
}

void VegaHarmonicProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);

    // ---- pitch tracking on the dry input
    for (int i = 0; i < n; ++i)
        if (tracker.push (0.5f * (L[i] + R[i])))
        {
            if (tracker.f0 > 0 && tracker.confidence > 0.6f)
            {
                f0s = (f0s <= 0 || std::abs (std::log2 (tracker.f0 / f0s)) > 0.08f) ? tracker.f0 : f0s + 0.35f * (tracker.f0 - f0s);
                unvoicedFor = 0;
            }
            else ++unvoicedFor;
        }
    uiF0.store (unvoicedFor < 30 ? f0s : 0.0f); uiConf.store (tracker.confidence);

    // ---- harmonic EQ coefficients follow the note
    const bool harmOn = p[kHarmOn]->load() > 0.5f;
    const float gain = p[kGain]->load(), oe = p[kOddEven]->load() * 0.01f, tilt = p[kTilt]->load() * 0.01f, q = p[kWidth]->load();
    const bool voiced = unvoicedFor < 30 && f0s > 0;
    const float target = voiced ? f0s : 0.0f;
    const bool paramsChanged = gain != lastParams[0] || oe != lastParams[1] || tilt != lastParams[2] || q != lastParams[3];
    if (harmOn && voiced && (paramsChanged || lastF0Set <= 0 || std::abs (target / lastF0Set - 1.0f) > 0.004f))
    {
        lastF0Set = target; lastParams[0] = gain; lastParams[1] = oe; lastParams[2] = tilt; lastParams[3] = q;
        for (int k = 1; k <= numHarm; ++k)
        {
            const double fk = target * k;
            const float g = (fk < sr * 0.45) ? juce::Decibels::decibelsToGain (harmonicGainDb (k, gain, oe, tilt)) : 1.0f;
            auto c = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, std::min (fk, sr * 0.45), q, g);
            for (auto& f : peaks[k - 1]) *f.coefficients = *c;
        }
    }
    else if (! voiced && lastF0Set > 0)
    {   // no clear note: relax to flat so nothing rings at a stale pitch
        for (auto& h : peaks) for (auto& f : h) { auto c = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 1000.0, 1.0, 1.0f); *f.coefficients = *c; }
        lastF0Set = -1;
    }

    // ---- sub generator filters
    const float range = p[kRange]->load();
    if (std::abs (range - lastRange) > 0.5f)
    {
        lastRange = range;
        auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, range, 0.707f);
        *subLp1.coefficients = *c; *subLp2.coefficients = *c;
        auto o = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, std::max (30.0f, range * 0.5f), 0.707f);
        for (int ch = 0; ch < 2; ++ch) { *outLp1[ch].coefficients = *o; *outLp2[ch].coefficients = *o; }
    }
    const bool subOn = p[kSubOn]->load() > 0.5f, subOnly = p[kSubOnly]->load() > 0.5f, divider = p[kSubMode]->load() < 0.5f;
    const float s1 = p[kSub1]->load() * 0.01f, s2 = p[kSub2]->load() * 0.01f, drive = p[kDrive]->load() * 0.01f;
    const float mix = p[kMix]->load() * 0.01f, outG = juce::Decibels::decibelsToGain (p[kOut]->load());
    const float atk = 1.0f - std::exp (-1.0f / (float) (sr * 0.005)), rel = 1.0f - std::exp (-1.0f / (float) (sr * 0.12));
    const bool trackedOk = voiced && f0s < range * 1.6f;

    float pk[2] {}, subPk = 0;
    for (int i = 0; i < n; ++i)
    {
        const float dl = L[i], dr = R[i];
        float yl = dl, yr = dr;
        if (harmOn && lastF0Set > 0)
            for (int k = 0; k < numHarm; ++k) { yl = peaks[k][0].processSample (yl); yr = peaks[k][1].processSample (yr); }

        float sub = 0;
        if (subOn)
        {
            const float x = subLp2.processSample (subLp1.processSample (0.5f * (dl + dr)));
            const float ax = std::abs (x);
            subEnv += (ax > subEnv ? atk : rel) * (ax - subEnv);
            if (divider)
            {   // flip-flop octave dividers on the low band's zero crossings (classic sub-harmonic synth)
                const float hyst = subEnv * 0.1f;
                if (prevX <= -hyst && x > hyst) { ff1 = ! ff1; if (ff1) ff2 = ! ff2; prevX = x; }
                else if (x < -hyst) prevX = x;
                const float sq = (ff1 ? 1.0f : -1.0f) * s1 + (ff2 ? 1.0f : -1.0f) * s2;
                sub = outLp2[0].processSample (outLp1[0].processSample (sq)) * subEnv * 1.5f;
            }
            else if (trackedOk)
            {   // pure sines locked to the tracked pitch
                ph1 += f0s * 0.5 / sr; if (ph1 >= 1) ph1 -= 1;
                ph2 += f0s * 0.25 / sr; if (ph2 >= 1) ph2 -= 1;
                sub = (float) (std::sin (juce::MathConstants<double>::twoPi * ph1) * s1 + std::sin (juce::MathConstants<double>::twoPi * ph2) * s2) * subEnv * 1.4f;
            }
            if (drive > 0) sub = std::tanh (sub * (1.0f + drive * 5.0f)) / (1.0f + drive * 2.0f);
            subPk = std::max (subPk, std::abs (sub));
        }
        float ol, orr;
        if (subOnly) { ol = orr = sub; }
        else { ol = dl + (yl + sub - dl) * mix; orr = dr + (yr + sub - dr) * mix; }
        ol *= outG; orr *= outG;
        L[i] = ol; R[i] = orr;
        pk[0] = std::max (pk[0], std::abs (ol)); pk[1] = std::max (pk[1], std::abs (orr));
    }
    uiSub.store (std::max (uiSub.load(), subPk));
    outL.store (std::max (outL.load(), pk[0])); outR.store (std::max (outR.load(), pk[1]));
}

void VegaHarmonicProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto s = apvts.copyState(); s.setProperty ("uiScale", uiScale.load(), nullptr);
    if (auto x = s.createXml()) copyXmlToBinary (*x, d);
}
void VegaHarmonicProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size))
    {
        auto s = juce::ValueTree::fromXml (*x);
        if (! s.hasType (apvts.state.getType())) return;
        uiScale.store ((float) s.getProperty ("uiScale", 1.0f));
        apvts.replaceState (s);
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VegaHarmonicProcessor(); }
