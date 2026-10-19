// Copycat: the plugin. Listens to the audio passing through (it is not changed), keeps the last 2 seconds, and on Capture
// analyses them with the engine in src/core. The editor is a plain text readout for now; the cat-ui editor from docs/mockup is on the ROADMAP.
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

    // message thread: copy the last 2 s, analyse, return the text report
    juce::String captureAndAnalyse();
    static juce::String describe(const copycat::Result& r);
    std::atomic<float> level { 0.f };

private:
    static constexpr int kSeconds = 2;
    std::vector<float> ring; std::atomic<int> writePos { 0 };
    double fs = 44100;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CopycatProcessor)
};
