// Copycat window, built from docs/mockup/copycat.html: header, capture bar, five views (Sound, Synth guess, Envelope & filter,
// EQ curve, Recipes), estimates panel and level meter. Fixed aspect ratio, resizable from every corner and edge, wood cheeks.
// Not built yet (needs the C++ Rebuild synth, see ROADMAP): the Original / Copy listening buttons, spectrum match, presets.
#include "Copycat.h"
#include <catui/CatLookAndFeel.h>
#include <catui/RenderGate.h>
#include <catui/Logo.h>
#include <cmath>

namespace {
using juce::Colour; using juce::Rectangle; using juce::Graphics; using juce::String;
const Colour kBg(0xff101114), kPanel(0xff17181c), kEdge(0xff2c2e35), kText(0xffe8e6e1), kDim(0xff8c909a), kGold(0xffffd100),
             kTeal(0xff3fd9c0), kPink(0xffff4fa8), kPurple(0xffa97bff);
constexpr int kCheek = 30, kContentW = 1140, kBaseW = kContentW + 2 * kCheek, kBaseH = 780;

juce::Font mono(float h, bool bold = false) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }
juce::Font sans(float h, bool bold = false) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)); }

String noteOf(double hz)
{
    if (hz < 20) return "-";
    const double m = 69 + 12 * std::log2(hz / 440.0); const int mi = (int) std::lround(m), c = (int) std::lround((m - mi) * 100);
    return String(copycat::noteName(mi)) + String(mi / 12 - 1) + (c >= 0 ? " +" : " ") + String(c) + " c";
}

// Flat button used for the capture bar, tabs and header links.
class Pill : public juce::Button
{
public:
    explicit Pill(const String& t, bool big = false) : juce::Button(t), large(big) {}
    void paintButton(Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced(0.5f); const bool on = getToggleState(), dis = !isEnabled();
        g.setColour(on ? kTeal.withAlpha(0.16f) : (over && !dis ? Colour(0xff25272e) : Colour(0xff1b1c21))); g.fillRoundedRectangle(r, 3.f);
        g.setColour(on ? kTeal : (dis ? kEdge : Colour(0xff3a3d46))); g.drawRoundedRectangle(r, 3.f, on ? 1.5f : 1.f);
        g.setColour(dis ? kDim.withAlpha(0.5f) : (on ? kTeal : (down ? kGold : kText)));
        g.setFont(sans(large ? 16.f : 13.f, true)); g.drawFittedText(getButtonText().toUpperCase(), getLocalBounds(), juce::Justification::centred, 1);
    }
private:
    bool large;
};

class ViewPane : public juce::Component
{
public:
    enum Mode { Sound, Synth, Env, Eq, Recipes };
    std::function<void(const String&)> onHint;
    void setMode(Mode m) { mode = m; repaint(); }
    void setCapture(const CopycatProcessor::Capture& c)
    {
        cap = c; hasData = c.ok;
        if (hasData) buildSpectrogram();
        repaint();
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colour(0xff060608)); g.setColour(kEdge); g.drawRect(getLocalBounds(), 1);
        if (!hasData)
        {
            g.setColour(kDim); g.setFont(sans(18.f)); g.drawFittedText("Play a sound through the plugin, then press Capture 2 s.\nOr use Load file.", getLocalBounds().reduced(40), juce::Justification::centred, 3);
            return;
        }
        switch (mode) { case Sound: paintSound(g); break; case Synth: paintSynth(g); break; case Env: paintEnv(g); break; case Eq: paintEq(g); break; case Recipes: paintRecipes(g); break; }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (!hasData || mode != Sound || !onHint) return;
        auto a = specArea(); auto p = e.getPosition();
        if (!a.contains(p)) { onHint("Hover the spectrogram to read a frequency and a note."); return; }
        const double hz = yToHz(p.y, a), t = (double) (p.x - a.getX()) / a.getWidth() * (double) cap.audio.size() / cap.fs;
        onHint(String(hz, 0) + " Hz  " + noteOf(hz) + "   at " + String(t, 2) + " s");
    }

private:
    CopycatProcessor::Capture cap; bool hasData = false; Mode mode = Sound; juce::Image spec;
    static constexpr double fMin = 30.0;
    double fMax() const { return std::min(12000.0, cap.fs * 0.5); }
    Rectangle<int> specArea() const { return getLocalBounds().withTrimmedLeft(52).withTrimmedBottom(26).withTrimmedTop(18).withTrimmedRight(14); }
    double yToHz(int y, Rectangle<int> a) const { const double t = 1.0 - (double) (y - a.getY()) / a.getHeight(); return fMin * std::pow(fMax() / fMin, juce::jlimit(0.0, 1.0, t)); }
    float hzToY(double hz, Rectangle<int> a) const { return (float) a.getBottom() - (float) (std::log(hz / fMin) / std::log(fMax() / fMin)) * (float) a.getHeight(); }

    static Colour heat(float t)
    {
        struct S { float p; juce::uint32 c; };
        static const S st[] = { { 0.f, 0xff04040a }, { 0.25f, 0xff3a0a6e }, { 0.5f, 0xffc2278f }, { 0.75f, 0xffff8a2a }, { 1.f, 0xffffe83a } };
        t = juce::jlimit(0.f, 1.f, t);
        for (int i = 1; i < 5; ++i) if (t <= st[i].p) return Colour(st[i - 1].c).interpolatedWith(Colour(st[i].c), (t - st[i - 1].p) / (st[i].p - st[i - 1].p));
        return Colour(st[4].c);
    }

    void buildSpectrogram()
    {
        const int W = 760, H = 480, N = 2048; const auto& x = cap.audio; if (x.size() < (size_t) N * 2) { spec = {}; return; }
        spec = juce::Image(juce::Image::RGB, W, H, true); const auto win = copycat::hann((size_t) N);
        std::vector<std::vector<float>> cols((size_t) W); float mx = 1e-9f;
        for (int c = 0; c < W; ++c)
        {
            const size_t from = (size_t) ((double) c / (W - 1) * (double) (x.size() - (size_t) N));
            cols[(size_t) c] = copycat::magSpec(x, from, (size_t) N, win);
            for (float v : cols[(size_t) c]) mx = std::max(mx, v);
        }
        const double binHz = cap.fs / N, top = std::min(12000.0, cap.fs * 0.5);
        for (int c = 0; c < W; ++c)
            for (int r = 0; r < H; ++r)
            {
                const double hz = fMin * std::pow(top / fMin, 1.0 - (double) r / (H - 1)); const double b = hz / binHz; const size_t i = (size_t) b;
                const auto& m = cols[(size_t) c]; if (i + 1 >= m.size()) continue;
                const float v = m[i] + (m[i + 1] - m[i]) * (float) (b - (double) i);
                spec.setPixelAt(c, r, heat((20.f * std::log10(v / mx + 1e-9f) + 75.f) / 75.f));
            }
    }

    void axisText(Graphics& g, const String& s, float x, float y, juce::Justification j, Colour c = kPurple) { g.setColour(c); g.setFont(mono(12.f)); g.drawText(s, juce::Rectangle<float>(x - 40, y - 8, 80, 16), j); }

    void paintSound(Graphics& g)
    {
        auto a = specArea(); if (spec.isValid()) g.drawImage(spec, a.toFloat());
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            if (f > fMax()) continue; const float y = hzToY(f, a);
            g.setColour(juce::Colours::white.withAlpha(0.07f)); g.drawHorizontalLine((int) y, (float) a.getX(), (float) a.getRight());
            axisText(g, f >= 1000 ? String((int) (f / 1000)) + "k" : String((int) f), (float) a.getX() - 22, y, juce::Justification::centredRight);
        }
        const double dur = (double) cap.audio.size() / cap.fs;
        for (int i = 0; i <= 4; ++i) axisText(g, String(dur * i / 4, 1) + " s", (float) a.getX() + (float) a.getWidth() * (float) i / 4.f, (float) a.getBottom() + 14.f, juce::Justification::centred);
        if (cap.r.f0 > 30) { const float y = hzToY(cap.r.f0, a); g.setColour(kGold.withAlpha(0.7f)); g.drawHorizontalLine((int) y, (float) a.getX(), (float) a.getRight()); g.setFont(mono(12.f)); g.drawText("root " + String(cap.r.noteName_), a.getX() + 4, (int) y - 16, 80, 14, juce::Justification::left); }
    }

    void paintSynth(Graphics& g)
    {
        auto a = getLocalBounds().reduced(24); const auto& r = cap.r;
        g.setColour(kDim); g.setFont(sans(13.f, true)); g.drawText("HARMONICS (LEVEL RELATIVE TO THE FUNDAMENTAL)", a.removeFromTop(20), juce::Justification::left);
        auto chart = a.removeFromTop(300).reduced(0, 10); const int n = (int) r.harmonicsDb.size(); const float bw = chart.getWidth() / (float) std::max(1, n);
        for (int dbl = 0; dbl >= -60; dbl -= 20) { const float y = chart.getY() + (float) (-dbl) / 60.f * chart.getHeight(); g.setColour(Colours_grid()); g.drawHorizontalLine((int) y, (float) chart.getX(), (float) chart.getRight()); axisText(g, String(dbl) + " dB", (float) chart.getX() + 30, y - 8, juce::Justification::left); }
        for (int i = 0; i < n; ++i)
        {
            const float v = juce::jlimit(0.f, 1.f, (float) (r.harmonicsDb[(size_t) i] + 60.0) / 60.f); const float h = v * chart.getHeight();
            g.setColour(i == 0 ? kGold : kPink.withAlpha(0.85f)); g.fillRect(chart.getX() + bw * (float) i + 6, chart.getBottom() - h, bw - 12, h);
            axisText(g, String(i + 1), chart.getX() + bw * ((float) i + 0.5f), (float) chart.getBottom() + 12, juce::Justification::centred);
        }
        a.removeFromTop(30); g.setColour(kText); g.setFont(sans(20.f, true));
        g.drawText("Oscillator: " + String(copycat::waveName(r.wave)) + ", " + String(r.voices) + " voice" + (r.voices > 1 ? "s" : ""), a.removeFromTop(30), juce::Justification::left);
        g.setColour(kDim); g.setFont(sans(15.f));
        g.drawFittedText(r.voices > 1 ? "Detune about " + String(r.detuneCents, 1) + " cents between voices. A rough estimate: the test sounds read about right, real recordings are not checked yet."
                                       : "No detune found: one clean voice.", a.removeFromTop(48), juce::Justification::topLeft, 2);
    }
    static Colour Colours_grid() { return juce::Colours::white.withAlpha(0.08f); }

    void paintEnv(Graphics& g)
    {
        auto a = getLocalBounds().reduced(24); const auto& e = cap.r.env;
        g.setColour(kDim); g.setFont(sans(13.f, true)); g.drawText("AMPLITUDE ENVELOPE (ADSR FIT)", a.removeFromTop(20), juce::Justification::left);
        auto top = a.removeFromTop(210).reduced(0, 8);
        const double total = e.attack + e.decay + 0.35 + e.release; auto X = [&](double t) { return (float) top.getX() + (float) (t / total) * (float) top.getWidth(); };
        auto Y = [&](double lvl) { return (float) top.getBottom() - (float) lvl * (float) top.getHeight(); };
        juce::Path p; p.startNewSubPath(X(0), Y(0)); p.lineTo(X(e.attack), Y(1)); p.lineTo(X(e.attack + e.decay), Y(e.sustain)); p.lineTo(X(e.attack + e.decay + 0.35), Y(e.sustain)); p.lineTo(X(total), Y(0));
        g.setColour(kPink.withAlpha(0.18f)); juce::Path f(p); f.closeSubPath(); g.fillPath(f); g.setColour(kPink); g.strokePath(p, juce::PathStrokeType(2.5f));
        g.setColour(kText); g.setFont(mono(13.f));
        g.drawText("A " + String(e.attack * 1000, 0) + " ms    D " + String(e.decay * 1000, 0) + " ms    S " + String(e.sustain * 100, 0) + " %    R " + String(e.release * 1000, 0) + " ms", a.removeFromTop(24), juce::Justification::left);
        a.removeFromTop(10); g.setColour(kDim); g.setFont(sans(13.f, true)); g.drawText("FILTER: LONG TERM SPECTRUM AND CUTOFF ESTIMATE", a.removeFromTop(20), juce::Justification::left);
        paintSpectrum(g, a.removeFromTop(220), true, false);
        g.setColour(kText); g.setFont(mono(13.f)); g.drawText("Low pass about " + String((int) cap.r.cutoffHz) + " Hz, " + String(cap.r.slope) + " dB/oct (rough estimate)", a.removeFromTop(24), juce::Justification::left);
    }

    void paintSpectrum(Graphics& g, Rectangle<int> a, bool cutoff, bool eqBands)
    {
        const auto& r = cap.r; if (r.longTerm.empty()) return; a = a.withTrimmedLeft(40);
        const double top = std::min(20000.0, cap.fs * 0.5), lo = 30.0; float mx = -200; for (float v : r.longTerm) mx = std::max(mx, v);
        auto X = [&](double hz) { return (float) a.getX() + (float) (std::log(hz / lo) / std::log(top / lo)) * (float) a.getWidth(); };
        for (double f : { 50.0, 100.0, 500.0, 1000.0, 5000.0, 10000.0 }) { g.setColour(Colours_grid()); g.drawVerticalLine((int) X(f), (float) a.getY(), (float) a.getBottom()); axisText(g, f >= 1000 ? String((int) (f / 1000)) + "k" : String((int) f), X(f), (float) a.getBottom() + 10, juce::Justification::centred); }
        juce::Path p; bool first = true;
        for (size_t i = 1; i < r.longTerm.size(); ++i)
        {
            const double hz = (double) i * r.longTermBinHz; if (hz < lo || hz > top) continue;
            const float y = (float) a.getBottom() - juce::jlimit(0.f, 1.f, (r.longTerm[i] - mx + 90.f) / 90.f) * (float) a.getHeight();
            if (first) { p.startNewSubPath(X(hz), y); first = false; } else p.lineTo(X(hz), y);
        }
        g.setColour(kTeal); g.strokePath(p, juce::PathStrokeType(1.5f));
        if (cutoff) { g.setColour(kGold); g.drawVerticalLine((int) X(cap.r.cutoffHz), (float) a.getY(), (float) a.getBottom()); g.setFont(mono(12.f)); g.drawText("cutoff", (int) X(cap.r.cutoffHz) + 4, a.getY(), 70, 14, juce::Justification::left); }
        if (eqBands) for (auto& b : r.eq)
        {
            const float x = X(b.freq), h = juce::jlimit(-12.f, 12.f, (float) b.gainDb) / 12.f * (float) a.getHeight() * 0.35f, mid = (float) a.getCentreY();
            g.setColour(b.gainDb >= 0 ? kGold : kPink); g.fillRect(x - 6, h >= 0 ? mid - h : mid, 12.f, std::abs(h)); axisText(g, String((int) b.freq) + " Hz", x, (float) a.getY() + 8, juce::Justification::centred, kText);
        }
    }

    void paintEq(Graphics& g)
    {
        auto a = getLocalBounds().reduced(24);
        g.setColour(kDim); g.setFont(sans(13.f, true)); g.drawText("LONG TERM SPECTRUM AND EQ IDEAS (GOLD = BOOST, PINK = CUT)", a.removeFromTop(20), juce::Justification::left);
        paintSpectrum(g, a.removeFromTop(330), false, true); a.removeFromTop(30);
        g.setColour(kText); g.setFont(mono(14.f));
        if (cap.r.eq.empty()) g.drawText("No clear EQ ideas for this sound.", a.removeFromTop(24), juce::Justification::left);
        for (auto& b : cap.r.eq) g.drawText(String((int) b.freq) + " Hz   " + (b.gainDb >= 0 ? "+" : "") + String(b.gainDb, 1) + " dB", a.removeFromTop(22), juce::Justification::left);
    }

    void paintRecipes(Graphics& g)
    {
        const auto& r = cap.r; auto a = getLocalBounds().reduced(30);
        g.setColour(kDim); g.setFont(sans(13.f, true)); g.drawText("RECIPE: HOW TO REBUILD THIS SOUND IN YOUR SYNTH", a.removeFromTop(24), juce::Justification::left);
        const String t = "1. Oscillator: " + String(copycat::waveName(r.wave)) + ", " + String(r.voices) + " voice(s)" + (r.voices > 1 ? ", detune about " + String(r.detuneCents, 1) + " cents" : "") + ".\n"
            "2. Pitch: " + String(r.noteName_) + " (" + String(r.f0, 1) + " Hz). Key looks like " + String(r.key) + (r.hasChord ? ", chord " + String(r.chord.name) : "") + ".\n"
            "3. Filter: low pass near " + String((int) r.cutoffHz) + " Hz, " + String(r.slope) + " dB per octave.\n"
            "4. Amp envelope: attack " + String(r.env.attack * 1000, 0) + " ms, decay " + String(r.env.decay * 1000, 0) + " ms, sustain " + String(r.env.sustain * 100, 0) + " %, release " + String(r.env.release * 1000, 0) + " ms.\n"
            "5. Tempo: " + (r.bpm > 0 ? String(r.bpm, 1) + " BPM" : String("not enough hits to tell")) + ".\n"
            "6. EQ ideas: " + (r.eq.empty() ? String("none") : [&] { String s; for (auto& b : r.eq) s << String((int) b.freq) << " Hz " << (b.gainDb >= 0 ? "+" : "") << String(b.gainDb, 1) << " dB   "; return s; }()) + "\n\n"
            "Every number is an estimate. Check it by ear against the original.";
        g.setColour(kText); g.setFont(sans(17.f)); g.drawFittedText(t, a, juce::Justification::topLeft, 14);
    }
};

class MeterPane : public juce::Component
{
public:
    float l = 0, r = 0;
    void paint(Graphics& g) override
    {
        g.fillAll(Colour(0xff060608)); g.setColour(kEdge); g.drawRect(getLocalBounds(), 1);
        auto a = getLocalBounds().reduced(8, 10);
        for (int db = 0; db >= -54; db -= 6) { const float y = (float) a.getY() + (float) (-db) / 54.f * (float) a.getHeight(); g.setColour(juce::Colours::white.withAlpha(0.08f)); g.drawHorizontalLine((int) y, (float) a.getX(), (float) a.getRight()); g.setColour(db == 0 ? kGold : kPurple); g.setFont(mono(11.f)); g.drawText(String(db), a.getCentreX() - 20, (int) y - 7, 40, 14, juce::Justification::centred); }
        auto bar = [&](float v, float x) { const float db = juce::Decibels::gainToDecibels(v, -60.f); const float h = juce::jlimit(0.f, 1.f, (db + 54.f) / 54.f) * (float) a.getHeight(); g.setColour(db > -1.f ? kPink : (db > -9.f ? kPurple : kTeal)); g.fillRect(x, (float) a.getBottom() - h, 16.f, h); };
        bar(l, (float) a.getX() + 6); bar(r, (float) a.getRight() - 22);
        g.setColour(kDim); g.setFont(mono(11.f)); g.drawText("L", a.getX() + 6, getHeight() - 14, 16, 12, juce::Justification::centred); g.drawText("R", a.getRight() - 22, getHeight() - 14, 16, 12, juce::Justification::centred);
    }
};

class CopycatEditor : public juce::AudioProcessorEditor
{
public:
    explicit CopycatEditor(CopycatProcessor& p) : AudioProcessorEditor(p), proc(p), gate(*this)
    {
        auto& c = content; addAndMakeVisible(c);
        for (auto* b : { &listen, &capture, &autoCap, &load, &guide, &about }) c.addAndMakeVisible(*b);
        listen.setClickingTogglesState(true); listen.setToggleState(true, juce::dontSendNotification); autoCap.setClickingTogglesState(true);
        listen.setTooltip("Show the live input level (the sound always passes through untouched).");
        capture.setTooltip("Analyse the last 2 seconds that passed through the plugin."); capture.onClick = [this] { doCapture(); };
        autoCap.setTooltip("Capture by itself: when a sound starts, wait 2 seconds, then analyse.");
        load.setTooltip("Analyse the first 2 seconds of an audio file."); load.onClick = [this] { chooseFile(); };
        guide.onClick = [] { juce::URL("https://github.com/SpeakingColors/copycat-vst/blob/main/docs/copycat-vst-manual.pdf").launchInDefaultBrowser(); };
        about.onClick = [] { juce::URL("https://ko-fi.com/studio87").launchInDefaultBrowser(); };
        const char* names[] = { "Sound", "Synth guess", "Envelope & filter", "EQ curve", "Recipes" };
        for (int i = 0; i < 5; ++i) { tabs[i] = std::make_unique<Pill>(names[i], true); c.addAndMakeVisible(*tabs[i]); tabs[i]->setRadioGroupId(7); tabs[i]->setClickingTogglesState(true); tabs[i]->onClick = [this, i] { selectTab(i); }; }
        tabs[0]->setToggleState(true, juce::dontSendNotification);
        for (auto* b : { &orig, &copy }) { c.addAndMakeVisible(*b); b->setEnabled(false); b->setTooltip("Needs the Rebuild synth, which is not built yet (see ROADMAP)."); }
        c.addAndMakeVisible(view); c.addAndMakeVisible(meter);
        view.onHint = [this](const String& s) { hint = s; content.repaint(hintArea); };
        constrainer.setFixedAspectRatio((double) kBaseW / kBaseH); constrainer.setSizeLimits(kBaseW / 2, kBaseH / 2, kBaseW * 2, kBaseH * 2);
        setConstrainer(&constrainer); setResizable(true, true); setSize(kBaseW, kBaseH);
        gate.onFrame = [this] { tick(); }; gate.start(20);
        shownCount = -1; refresh();
        if (const char* f = std::getenv("CAT_TOUR_LOAD"))   // documentation tool for the screenshots, never set for users
        {
            const int tab = std::getenv("CAT_TOUR_TAB") ? juce::jlimit(0, 4, juce::String(std::getenv("CAT_TOUR_TAB")).getIntValue()) : 0;
            juce::Timer::callAfterDelay(1500, [sp = juce::Component::SafePointer<CopycatEditor>(this), path = juce::String(f), tab] {
                if (sp == nullptr) return; sp->proc.loadFile(juce::File(path)); sp->tabs[tab]->setToggleState(true, juce::dontSendNotification); sp->selectTab(tab); sp->refresh(); });
        }
    }

    void paint(Graphics& g) override { g.fillAll(kBg); catui::drawWoodCheeks(g, getLocalBounds(), juce::roundToInt(kCheek * scale())); }
    void resized() override { const float s = scale(); content.setBounds(juce::roundToInt(kCheek * s), 0, kContentW, kBaseH); content.setTransform(juce::AffineTransform::scale(s)); layout(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override { const auto p = e.getEventRelativeTo(this).getPosition(); if (p.x < kCheek * scale() || p.x > getWidth() - kCheek * scale()) setSize(kBaseW, kBaseH); }

private:
    CopycatProcessor& proc; juce::ComponentBoundsConstrainer constrainer; catui::RenderGate gate;
    struct Content : juce::Component { CopycatEditor& o; explicit Content(CopycatEditor& e) : o(e) {} void paint(Graphics& g) override { o.paintContent(g); } } content { *this };
    Pill listen { "Listen" }, capture { "Capture 2 s" }, autoCap { "Auto capture" }, load { "Load file" }, guide { "Guide" }, about { "About & Support" }, orig { "Original" }, copy { "Copy" };
    std::unique_ptr<Pill> tabs[5]; ViewPane view; MeterPane meter;
    Rectangle<int> hintArea { 0, 722, 810, 30 }, statusArea, estArea; String hint { "Hover the spectrogram to read a frequency and a note." };
    int shownCount = -1; bool armed = false; juce::uint32 armedAt = 0; std::unique_ptr<juce::FileChooser> chooser;

    float scale() const { return (float) getWidth() / (float) kBaseW; }
    void layout()
    {
        guide.setBounds(900, 20, 90, 34); about.setBounds(1000, 20, 130, 34);
        listen.setBounds(268, 112, 100, 36); capture.setBounds(376, 112, 130, 36); autoCap.setBounds(514, 112, 140, 36); load.setBounds(662, 112, 110, 36);
        statusArea = { 790, 112, 340, 36 };
        const int tw = 214; for (int i = 0; i < 5; ++i) tabs[i]->setBounds(14 + i * (tw + 6), 162, tw, 38);
        view.setBounds(14, 210, 810, 500); estArea = { 836, 210, 292, 500 };
        orig.setBounds(848, 520, 120, 34); copy.setBounds(976, 520, 100, 34); meter.setBounds(848, 590, 268, 110);
        hintArea = { 14, 718, 810, 32 };
    }
    void selectTab(int i) { view.setMode((ViewPane::Mode) i); hint = i == 0 ? "Hover the spectrogram to read a frequency and a note." : String(); content.repaint(); }
    void doCapture() { proc.captureNow(); refresh(); }
    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser>("Choose an audio file", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) { proc.loadFile(fc.getResult()); refresh(); } });
    }
    void refresh() { if (proc.captureCount() != shownCount) { shownCount = proc.captureCount(); view.setCapture(proc.last()); } content.repaint(); }
    void tick()
    {
        const bool lv = listen.getToggleState(); const float l = proc.levelL.load(), r = proc.levelR.load();
        meter.l = lv ? l : 0.f; meter.r = lv ? r : 0.f; meter.repaint();
        if (autoCap.getToggleState())
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (!armed && std::max(l, r) > 0.05f) { armed = true; armedAt = now; }
            else if (armed && now - armedAt >= 2100) { armed = false; doCapture(); }
        }
        else armed = false;
        if (proc.captureCount() != shownCount) refresh();
    }

    void row(Graphics& g, int y, const String& label, const String& value, const String& sub)
    {
        g.setColour(kDim); g.setFont(mono(12.f)); g.drawText(label, estArea.getX() + 12, y, 130, 18, juce::Justification::left);
        g.setColour(kGold); g.setFont(mono(17.f)); g.drawText(value, estArea.getX() + 100, y - 3, estArea.getWidth() - 112, 22, juce::Justification::right);
        g.setColour(kDim); g.setFont(mono(12.f)); g.drawText(sub, estArea.getX() + 40, y + 20, estArea.getWidth() - 52, 16, juce::Justification::right);
        g.setColour(kEdge); g.drawHorizontalLine(y + 40, (float) estArea.getX() + 12, (float) estArea.getRight() - 12);
    }
    void paintContent(Graphics& g)
    {
        g.fillAll(kBg);
        // header
        catui::drawCatHead(g, { 22, 14, 60, 56 }, kText, kBg);
        g.setColour(kText); g.setFont(sans(38.f, true)); g.drawText("COPYCAT", 96, 8, 300, 46, juce::Justification::left);
        g.setColour(kGold); g.setFont(sans(14.f, true)); g.drawText("STUDIO 87", 98, 52, 200, 20, juce::Justification::left);
        g.setColour(kEdge); g.drawHorizontalLine(84, 14.f, (float) kContentW - 14);
        // capture bar
        auto bar = Rectangle<int>(14, 98, kContentW - 28, 62).toFloat(); g.setColour(kPanel); g.fillRoundedRectangle(bar, 4.f); g.setColour(kEdge); g.drawRoundedRectangle(bar, 4.f, 1.f);
        g.setColour(kDim); g.setFont(sans(14.f)); g.drawText("CAPTURE", 28, 118, 120, 24, juce::Justification::left);
        g.setColour(kDim); g.setFont(sans(14.f)); g.drawFittedText(proc.status(), statusArea, juce::Justification::centredRight, 2);
        // estimates
        auto ep = estArea.toFloat(); g.setColour(kPanel); g.fillRoundedRectangle(ep, 4.f); g.setColour(kEdge); g.drawRoundedRectangle(ep, 4.f, 1.f);
        g.setColour(kDim); g.setFont(sans(13.f, true)); g.drawText("MUSICAL ESTIMATES", estArea.getX() + 12, estArea.getY() + 8, 260, 20, juce::Justification::left);
        const auto& c = proc.last();
        if (c.ok)
        {
            const auto& r = c.r; int y = estArea.getY() + 38;
            row(g, y, "KEY", String(r.key), "match " + String(juce::roundToInt(r.keyStrength * 100.0)) + " %"); y += 42;
            row(g, y, "CHORD NOW", r.hasChord ? String(r.chord.name) : String("-"), r.hasChord ? String(r.chord.notes.empty() ? "" : r.chord.notes.front()) : String()); y += 42;
            row(g, y, "ROOT NOTE", String(r.noteName_), String(r.f0, 1) + " Hz . " + String(r.cents) + " cents"); y += 42;
            row(g, y, "TEMPO", r.bpm > 0 ? String(juce::roundToInt(r.bpm)) : String("-"), r.bpm > 0 ? "from " + String(r.onsets) + " onsets" : String("not enough hits"));
            g.setColour(kDim); g.setFont(sans(13.f, true)); g.drawText("INSTANT COPY", estArea.getX() + 12, 400, 200, 20, juce::Justification::left);
            g.setFont(mono(12.f)); g.drawText("REBUILD", estArea.getX() + 12, 436, 90, 18, juce::Justification::left);
            g.setColour(kGold); g.drawText(String(copycat::waveName(r.wave)) + " x" + String(r.voices) + " LP " + String(r.cutoffHz / 1000.0, 2) + " kHz", estArea.getX() + 90, 436, estArea.getWidth() - 102, 18, juce::Justification::right);
        }
        else { g.setColour(kDim); g.setFont(sans(15.f)); g.drawFittedText("Nothing captured yet.", estArea.reduced(16).withHeight(60).translated(0, 36), juce::Justification::topLeft, 2); }
        g.setColour(kDim); g.setFont(sans(13.f, true)); g.drawText("LEVEL", estArea.getX() + 12, 566, 200, 20, juce::Justification::left);
        // hint line and footer
        g.setColour(kPanel); g.fillRoundedRectangle(hintArea.toFloat(), 4.f); g.setColour(kEdge); g.drawRoundedRectangle(hintArea.toFloat(), 4.f, 1.f);
        g.setColour(kGold); g.setFont(mono(13.f)); g.drawText(hint, hintArea.reduced(12, 0), juce::Justification::centredLeft);
        g.setColour(kEdge); g.drawHorizontalLine(756, 14.f, (float) kContentW - 14);
        g.setColour(kText); g.setFont(sans(13.f, true)); g.drawText("STUDIO 87", 14, 758, 100, 20, juce::Justification::left);
        g.setColour(kDim); g.setFont(sans(13.f)); g.drawText("COPYCAT   WINDOW " + String(juce::roundToInt(scale() * 100)) + " %", 100, 758, 400, 20, juce::Justification::left);
    }
};
}

juce::AudioProcessorEditor* CopycatProcessor::createEditor() { return new CopycatEditor(*this); }
