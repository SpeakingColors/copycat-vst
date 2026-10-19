#include "Copycat.h"

CopycatProcessor::CopycatProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void CopycatProcessor::prepareToPlay(double sr, int) { fs = sr; ring.assign((size_t) (sr * kSeconds), 0.f); writePos = 0; }

void CopycatProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    // audio passes through untouched; we only copy it into a ring
    const int n = buf.getNumSamples(), chans = buf.getNumChannels(); if (chans < 1 || ring.empty()) return;
    int w = writePos.load(); float pk = 0; const int size = (int) ring.size();
    for (int i = 0; i < n; ++i)
    {
        float s = buf.getSample(0, i); if (chans > 1) s = 0.5f * (s + buf.getSample(1, i));
        ring[(size_t) w] = s; w = (w + 1) % size; pk = std::max(pk, std::fabs(s));
    }
    writePos.store(w); level.store(std::max(pk, level.load() * 0.9f));
}

juce::String CopycatProcessor::captureAndAnalyse()
{
    if (ring.empty()) return "Not ready: press play in the host first.";
    std::vector<float> x(ring.size()); const int w = writePos.load(); const size_t size = ring.size();
    for (size_t i = 0; i < size; ++i) x[i] = ring[((size_t) w + i) % size];          // oldest first
    copycat::Result r;
    if (!copycat::analyse(x, fs, r)) return "Could not find a clear sound in the last 2 seconds.";
    return describe(r);
}

juce::String CopycatProcessor::describe(const copycat::Result& r)
{
    juce::String s;
    s << "Note: " << r.noteName_ << " (" << juce::String(r.f0, 1) << " Hz, " << (r.cents >= 0 ? "+" : "") << r.cents << " cents)\n";
    s << "Key: " << r.key << "\n";
    if (r.hasChord) s << "Chord: " << r.chord.name << "\n";
    s << "Tempo: " << (r.bpm > 0 ? juce::String(r.bpm, 1) + " BPM" : juce::String("not enough hits")) << "\n";
    s << "Oscillator: " << copycat::waveName(r.wave) << ", " << r.voices << " voice(s)" << (r.voices > 1 ? ", detune " + juce::String(r.detuneCents, 1) + " cents" : juce::String()) << "\n";
    s << "Filter: about " << juce::String((int) r.cutoffHz) << " Hz, " << r.slope << " dB/oct\n";
    s << "Envelope: attack " << juce::String(r.env.attack * 1000.0, 0) << " ms, decay " << juce::String(r.env.decay * 1000.0, 0) << " ms, sustain "
      << juce::String(r.env.sustain * 100.0, 0) << " %, release " << juce::String(r.env.release * 1000.0, 0) << " ms\n";
    if (!r.eq.empty()) { s << "EQ ideas:"; for (auto& b : r.eq) s << "  " << juce::String((int) b.freq) << " Hz " << (b.gainDb >= 0 ? "+" : "") << juce::String(b.gainDb, 1) << " dB"; s << "\n"; }
    return s;
}

namespace {
class CopycatEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CopycatEditor(CopycatProcessor& p) : AudioProcessorEditor(p), proc(p)
    {
        addAndMakeVisible(go); addAndMakeVisible(out); go.setButtonText("Capture and analyse the last 2 seconds");
        out.setMultiLine(true); out.setReadOnly(true); out.setText("Play the sound you want to copy, then press the button.");
        go.onClick = [this] { out.setText(proc.captureAndAnalyse()); };
        setSize(520, 360); startTimerHz(10);
    }
    void resized() override { auto r = getLocalBounds().reduced(8); go.setBounds(r.removeFromTop(32)); r.removeFromTop(6); out.setBounds(r); }
    void timerCallback() override { go.setColour(juce::TextButton::buttonColourId, proc.level.load() > 0.01f ? juce::Colours::seagreen : juce::Colours::darkgrey); }
private:
    CopycatProcessor& proc; juce::TextButton go; juce::TextEditor out;
};
}
juce::AudioProcessorEditor* CopycatProcessor::createEditor() { return new CopycatEditor(*this); }
