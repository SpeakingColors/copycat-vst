// Copycat: the plugin. Listens to the audio passing through (it is not changed), keeps the last 2 seconds, and on Capture
// analyses them with the engine in src/core. The editor (CopycatEditor.cpp) follows docs/mockup.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/CopycatAnalysis.h"
#include <atomic>

class CopycatProcessor : public juce::AudioProcessor
{
public:
    CopycatProcessor();
    const juce::String getName() const override { return "Copycat"; }
    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == l.getMainInputChannelSet() && !l.getMainOutputChannelSet().isDisabled(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // message thread: copy the last 2 s and analyse them. Returns true when a clear sound was found; status() says what happened.
    bool captureNow();
    // message thread: analyse the first 2 s of an audio file
    bool loadFile(const juce::File& f);
    static juce::String describe(const copycat::Result& r);
    std::atomic<float> levelL { 0.f }, levelR { 0.f };

    struct Capture { std::vector<float> audio; double fs = 44100; copycat::Result r; bool ok = false; };
    const Capture& last() const { return lastCapture; }
    const juce::String& status() const { return statusText; }
    int captureCount() const { return captures; }   // goes up on every successful capture, so the editor knows to redraw

private:
    static constexpr int kSeconds = 2;
    std::vector<float> ring; std::atomic<int> writePos { 0 };
    double fs = 44100;
    bool analyseAudio(std::vector<float> x, double sampleRate, const juce::String& source);
    Capture lastCapture; juce::String statusText { "Play the sound you want to copy, then press Capture 2 s." }; int captures = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CopycatProcessor)
};
