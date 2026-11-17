#include "PluginEditor.h"

namespace
{
class HarmonicView : public juce::Component
{
public:
    explicit HarmonicView (VegaHarmonicProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        vegaui::drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced (14.0f, 10.0f);
        auto& a = proc.apvts;
        const float f0 = proc.uiF0.load();
        auto top = r.removeFromTop (24.0f);
        g.setFont (VegaFonts::mono (16.0f));
        if (f0 > 0)
        {
            const double midi = 69.0 + 12.0 * std::log2 (f0 / 440.0);
            const int nn = juce::roundToInt (midi);
            g.setColour (VegaColours::accent);
            g.drawText (juce::MidiMessage::getMidiNoteName (nn, true, true, 3) + "   " + juce::String (f0, 1) + " Hz", top, juce::Justification::centredLeft);
        }
        else { g.setColour (VegaColours::textDim); g.drawText ("listening for a note...", top, juce::Justification::centredLeft); }
        g.setFont (VegaFonts::ui (9.5f, true)); g.setColour (VegaColours::textDim);
        g.drawText ("HARMONIC SERIES (dB)", top, juce::Justification::centredRight);
        // bars for harmonics 1..8 plus the sub octaves
        const float gain = a.getRawParameterValue ("harmGain")->load(), oe = a.getRawParameterValue ("oddEven")->load() * 0.01f, tilt = a.getRawParameterValue ("tilt")->load() * 0.01f;
        const bool harmOn = a.getRawParameterValue ("harmOn")->load() > 0.5f, subOn = a.getRawParameterValue ("subOn")->load() > 0.5f;
        const int cols = 10; const float cw = r.getWidth() / cols, mid = r.getCentreY();
        g.setColour (juce::Colour (0xff2a2a2a)); g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());
        for (int c = 0; c < cols; ++c)
        {
            const float x = r.getX() + c * cw + cw * 0.2f, w = cw * 0.6f;
            float db = 0; juce::String label; juce::Colour col = VegaColours::accent;
            if (c < 2)
            {
                const float lvl = a.getRawParameterValue (c == 0 ? "sub2" : "sub1")->load() * 0.01f;
                db = subOn ? lvl * 18.0f : 0.0f; label = c == 0 ? "1/4" : "1/2"; col = VegaColours::gold;
            }
            else { const int k = c - 1; db = harmOn ? VegaHarmonicProcessor::harmonicGainDb (k, gain, oe, tilt) : 0.0f; label = juce::String (k) + "x"; }
            const float h = juce::jlimit (-1.0f, 1.0f, db / 18.0f) * (r.getHeight() * 0.5f - 16);
            g.setColour (col.withAlpha (0.8f));
            if (h >= 0) g.fillRect (x, mid - h, w, h); else g.fillRect (x, mid, w, -h);
            g.setColour (VegaColours::textDim); g.setFont (VegaFonts::ui (9.5f, true));
            g.drawText (label, (int) x - 6, (int) r.getBottom() - 12, (int) w + 12, 12, juce::Justification::centred);
            if (f0 > 0) g.drawText (juce::String (juce::roundToInt (c < 2 ? f0 / (c == 0 ? 4 : 2) : f0 * (c - 1))), (int) x - 10, (int) r.getY(), (int) w + 20, 12, juce::Justification::centred);
        }
        const float sub = proc.uiSub.exchange (0.0f);
        g.setColour (VegaColours::gold.withAlpha (0.25f + 0.75f * std::min (1.0f, sub * 3.0f))); g.fillEllipse (r.getX() + cw * 2 - 10, r.getBottom() - 30, 8, 8);
    }
private:
    VegaHarmonicProcessor& proc;
};
}

class VegaHarmonicEditor::Main : public juce::Component
{
public:
    explicit Main (VegaHarmonicProcessor& p) : proc (p), a (p.apvts), view (p),
        harmOn (a, "harmOn", "Harmonic EQ"), gain (a, "harmGain", "HARMONICS", true), oddEven (a, "oddEven", "Odd / Even"), tilt (a, "tilt", "Tilt"), width (a, "width", "Focus"),
        subOn (a, "subOn", "Sub Gen"), mode (a, "subMode", "Mode"), sub1 (a, "sub1", "SUB -1 OCT", true), sub2 (a, "sub2", "Sub -2 Oct"), range (a, "subRange", "Range"), drive (a, "subDrive", "Drive"),
        subOnly (a, "subOnly", "Listen Sub"), mix (a, "mix", "Mix"), output (a, "output", "Output")
    {
        for (auto* c : std::initializer_list<juce::Component*> { &view, &harmOn, &gain, &oddEven, &tilt, &width, &subOn, &mode, &sub1, &sub2, &range, &drive, &subOnly, &mix, &output, &meter })
            addAndMakeVisible (c);
    }
    void refresh() { view.repaint(); meter.setLevels (proc.outL.exchange (0.0f), proc.outR.exchange (0.0f)); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (VegaColours::background);
        vegaui::drawTopBar (g, W, "HARMONIC  VP-23");
        vegaui::drawBody (g, { 0, 35, W, H - 65 });
        vegaui::drawSection (g, { 0, 232, 440, 298 }, "PITCH-TRACKING HARMONIC EQ", false);
        vegaui::drawSection (g, { 440, 232, 450, 298 }, "SUBHARMONIC GENERATOR");
        vegaui::drawSection (g, { 890, 232, W - 890, 298 }, "OUTPUT");
        vegaui::drawBottomBar (g, { 0, H - 30, W, 30 }, "BANDS FOLLOW THE NOTE YOU PLAY  -  SUB GENERATOR REBUILDS LOW END ON THIN 808S, BASS AND KICKS");
    }
    void resized() override
    {
        view.setBounds (12, 44, 950, 180); meter.setBounds (972, 40, 76, 184);
        harmOn.setBounds (14, 250, 70, 80); gain.setBounds (90, 250, 140, 170); oddEven.setBounds (240, 250, 90, 112); tilt.setBounds (334, 250, 90, 112);
        width.setBounds (240, 380, 90, 112);
        subOn.setBounds (452, 250, 70, 80); mode.setBounds (452, 446, 210, 46); sub1.setBounds (530, 250, 140, 170);
        sub2.setBounds (676, 250, 90, 112); range.setBounds (770, 250, 110, 112); drive.setBounds (676, 380, 90, 112); subOnly.setBounds (790, 400, 80, 80);
        mix.setBounds (904, 250, 70, 112); output.setBounds (978, 250, 70, 112);
    }
private:
    VegaHarmonicProcessor& proc; juce::AudioProcessorValueTreeState& a;
    HarmonicView view;
    vegaui::Toggle harmOn; vegaui::Knob gain, oddEven, tilt, width; vegaui::Toggle subOn; vegaui::Choice mode; vegaui::Knob sub1, sub2, range, drive; vegaui::Toggle subOnly;
    vegaui::Knob mix, output; vegaui::StereoMeter meter;
};

VegaHarmonicEditor::VegaHarmonicEditor (VegaHarmonicProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    main = std::make_unique<Main> (p);
    addAndMakeVisible (*main);
    constrainer.setFixedAspectRatio ((double) W / H);
    constrainer.setSizeLimits (W / 2, H / 2, W * 2, H * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const float s = juce::jlimit (0.5f, 2.0f, proc.uiScale.load());
    setSize (juce::roundToInt (W * s), juce::roundToInt (H * s));
    startTimerHz (30);
}
VegaHarmonicEditor::~VegaHarmonicEditor() { stopTimer(); setConstrainer (nullptr); main.reset(); setLookAndFeel (nullptr); }
void VegaHarmonicEditor::timerCallback() { main->refresh(); }
void VegaHarmonicEditor::resized()
{
    main->setBounds (0, 0, W, H);
    const float s = (float) getWidth() / (float) W;
    main->setTransform (juce::AffineTransform::scale (s));
    proc.uiScale.store (s);
}
juce::AudioProcessorEditor* VegaHarmonicProcessor::createEditor() { return new VegaHarmonicEditor (*this); }
