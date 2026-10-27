#include "Copycat.h"

CopycatProcessor::CopycatProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void CopycatProcessor::prepareToPlay(double sr, int) { fs = sr; ring.assign((size_t) (sr * kSeconds), 0.f); writePos = 0; }

void CopycatProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    // audio passes through untouched; we only copy it into a ring
    const int n = buf.getNumSamples(), chans = buf.getNumChannels(); if (chans < 1 || ring.empty()) return;
    int w = writePos.load(); float pkL = 0, pkR = 0; const int size = (int) ring.size();
    for (int i = 0; i < n; ++i)
    {
        const float l = buf.getSample(0, i), r = chans > 1 ? buf.getSample(1, i) : l; const float s = chans > 1 ? 0.5f * (l + r) : l;
        ring[(size_t) w] = s; w = (w + 1) % size; pkL = std::max(pkL, std::fabs(l)); pkR = std::max(pkR, std::fabs(r));
    }
    writePos.store(w); levelL.store(std::max(pkL, levelL.load() * 0.9f)); levelR.store(std::max(pkR, levelR.load() * 0.9f));
}

bool CopycatProcessor::analyseAudio(std::vector<float> x, double sampleRate, const juce::String& source)
{
    copycat::Result r;
    if (!copycat::analyse(x, sampleRate, r)) { statusText = "Could not find a clear sound in " + source + "."; return false; }
    lastCapture.audio = std::move(x); lastCapture.fs = sampleRate; lastCapture.r = std::move(r); lastCapture.ok = true; ++captures;
    statusText = "Captured " + juce::String((double) lastCapture.audio.size() / sampleRate, 1) + " s from " + source;
    return true;
}

bool CopycatProcessor::captureNow()
{
    if (ring.empty()) { statusText = "Not ready: press play in the host first."; return false; }
    std::vector<float> x(ring.size()); const int w = writePos.load(); const size_t size = ring.size();
    for (size_t i = 0; i < size; ++i) x[i] = ring[((size_t) w + i) % size];          // oldest first
    return analyseAudio(std::move(x), fs, "the input");
}

bool CopycatProcessor::loadFile(const juce::File& f)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(f));
    if (rd == nullptr) { statusText = "Could not read " + f.getFileName() + "."; return false; }
    const int n = (int) std::min<juce::int64>(rd->lengthInSamples, (juce::int64) (rd->sampleRate * 2.0));
    juce::AudioBuffer<float> b((int) std::max<unsigned>(1, rd->numChannels), n); rd->read(&b, 0, n, 0, true, true);
    std::vector<float> x((size_t) n); for (int i = 0; i < n; ++i) { float s = 0; for (int c = 0; c < b.getNumChannels(); ++c) s += b.getSample(c, i); x[(size_t) i] = s / (float) b.getNumChannels(); }
    return analyseAudio(std::move(x), rd->sampleRate, f.getFileName());
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

// createEditor() lives in CopycatEditor.cpp
