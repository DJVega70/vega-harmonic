#include "../Source/PluginProcessor.h"
#include <iostream>
static int fails = 0;
static void check (bool ok, const juce::String& m) { std::cout << (ok ? "PASS " : "FAIL ") << m << "\n"; if (! ok) ++fails; }
static void set (VegaHarmonicProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
const double fs = 48000; const int blk = 480;
static std::vector<float> run (VegaHarmonicProcessor& p, int n, std::function<float (int)> gen)
{
    std::vector<float> out; juce::AudioBuffer<float> b (2, blk); juce::MidiBuffer m;
    for (int pos = 0; pos < n; pos += blk)
    {
        for (int i = 0; i < blk; ++i) { const float x = gen (pos + i); b.setSample (0, i, x); b.setSample (1, i, x); }
        p.processBlock (b, m);
        for (int i = 0; i < blk; ++i) out.push_back (b.getSample (0, i));
    }
    return out;
}
static double mag (const std::vector<float>& v, double f, int a, int len)
{
    double re = 0, im = 0;
    for (int i = 0; i < len; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * i / len); re += v[(size_t) (a + i)] * w * std::cos (2 * juce::MathConstants<double>::pi * f * i / fs); im += v[(size_t) (a + i)] * w * std::sin (2 * juce::MathConstants<double>::pi * f * i / fs); }
    return std::sqrt (re * re + im * im) * 4.0 / len;
}
// band-limited saw with 8 harmonics + an inharmonic partial
static float saw (int i, double f0, double extra = 0)
{
    double s = 0; for (int k = 1; k <= 12; ++k) s += std::sin (2 * juce::MathConstants<double>::pi * f0 * k * i / fs) / k;
    if (extra > 0) s += 0.3 * std::sin (2 * juce::MathConstants<double>::pi * extra * i / fs);
    return (float) (0.3 * s);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto make = [&] { auto* p = new VegaHarmonicProcessor(); p->setPlayConfigDetails (2, 2, fs, blk); p->prepareToPlay (fs, blk); return std::unique_ptr<VegaHarmonicProcessor> (p); };
    {
        auto p = make(); set (*p, "harmOn", 0); set (*p, "subOn", 0);
        run (*p, 48000, [] (int i) { return saw (i, 110); });
        check (std::abs (p->uiF0.load() - 110) < 1.5f, "pitch tracker finds 110 Hz (" + juce::String (p->uiF0.load(), 2) + ")");
        auto p2 = make(); run (*p2, 48000, [] (int i) { return saw (i, 55); });
        check (std::abs (p2->uiF0.load() - 55) < 1.0f, "pitch tracker finds 55 Hz bass (" + juce::String (p2->uiF0.load(), 2) + ")");
    }
    {   // +12 dB harmonics: the 3rd harmonic of a 110 Hz saw rises, an inharmonic partial at 500 Hz does not
        auto ref = make(); set (*ref, "harmOn", 0); set (*ref, "subOn", 0);
        auto eq = make(); set (*eq, "harmGain", 12); set (*eq, "subOn", 0); set (*eq, "width", 30);
        auto gen = [] (int i) { return saw (i, 110, 500); };
        auto a = run (*ref, 96000, gen), b = run (*eq, 96000, gen);
        const double h3 = 20 * std::log10 (mag (b, 330, 48000, 48000) / mag (a, 330, 48000, 48000));
        const double in = 20 * std::log10 (mag (b, 500, 48000, 48000) / mag (a, 500, 48000, 48000));
        check (h3 > 9 && std::abs (in) < 3.0, "harmonic EQ follows the note: 3rd harmonic +" + juce::String (h3, 1) + " dB, 500 Hz partial " + juce::String (in, 1) + " dB");
    }
    {   // even-only: 2nd harmonic boosted, 3rd not
        auto ref = make(); set (*ref, "harmOn", 0); set (*ref, "subOn", 0);
        auto eq = make(); set (*eq, "harmGain", 12); set (*eq, "oddEven", 100); set (*eq, "subOn", 0); set (*eq, "width", 12);
        auto gen = [] (int i) { return saw (i, 110); };
        auto a = run (*ref, 96000, gen), b = run (*eq, 96000, gen);
        const double h2 = 20 * std::log10 (mag (b, 220, 48000, 48000) / mag (a, 220, 48000, 48000)), h3 = 20 * std::log10 (mag (b, 330, 48000, 48000) / mag (a, 330, 48000, 48000));
        check (h2 > 9 && h3 < 3, "even-only: 2nd +" + juce::String (h2, 1) + " dB, 3rd " + juce::String (h3, 1) + " dB");
    }
    for (int mode = 0; mode < 2; ++mode)
    {   // sub generator on a 100 Hz bass note: energy at 50 Hz (and 25 Hz when -2 oct is up)
        auto p = make(); set (*p, "harmOn", 0); set (*p, "subMode", (float) mode); set (*p, "sub1", 80); set (*p, "sub2", 60); set (*p, "subRange", 160);
        auto o = run (*p, 96000, [] (int i) { return 0.4f * (float) std::sin (2 * juce::MathConstants<double>::pi * 100 * i / fs); });
        const double m50 = mag (o, 50, 48000, 48000), m25 = mag (o, 25, 48000, 48000), m100 = mag (o, 100, 48000, 48000);
        check (m50 > 0.05 && m25 > 0.03 && std::abs (m100 - 0.4) < 0.08, juce::String (mode == 0 ? "divider" : "tracked") + " sub: 50 Hz " + juce::String (m50, 3) + ", 25 Hz " + juce::String (m25, 3) + ", dry 100 Hz kept " + juce::String (m100, 3));
        auto q = run (*p, 48000, [] (int) { return 0.0f; });
        double tail = 0; for (int i = 24000; i < 48000; ++i) tail = std::max (tail, (double) std::abs (q[(size_t) i]));
        check (tail < 0.01, "sub stops when the input stops (" + juce::String (tail, 4) + ")");
    }
    {   // sub ignores notes above its range
        auto p = make(); set (*p, "harmOn", 0); set (*p, "subMode", 1); set (*p, "subRange", 120);
        auto o = run (*p, 96000, [] (int i) { return 0.4f * (float) std::sin (2 * juce::MathConstants<double>::pi * 440 * i / fs); });
        check (mag (o, 220, 48000, 48000) < 0.01, "no sub added to a 440 Hz lead");
    }
    {
        auto p = make(); juce::Random r (2); auto o = run (*p, 96000, [&r] (int) { return r.nextFloat() - 0.5f; });
        bool fin = true; for (float v : o) fin &= std::isfinite (v);
        check (fin, "noise input stays finite");
    }
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << "\n";
    return fails == 0 ? 0 : 1;
}
