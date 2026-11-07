#pragma once

#include "tripan/Gains.h"
#include "tripan/Limits.h"
#include "tripan/TriangulationExchange.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

class TriPanAudioProcessor final : public juce::AudioProcessor,
                                   private juce::Timer
{
public:
    TriPanAudioProcessor();
    ~TriPanAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    /** Number of sources / speakers in use: the "Number of ..." parameter clamped to 1..channels of
        the track (0 if the track has no such channels). Safe to call from any thread. */
    int getActiveSources() const noexcept;
    int getActiveSpeakers() const noexcept;

    /** Message thread. Places the active sources and speakers on rings (speakers on radius 5 m,
        sources on radius 2.5 m, first speaker at front). */
    void resetLayoutToRings();

    /** Message thread. Rebuilds and publishes the triangulation if the active speaker positions
        or count changed. Called by a timer; also call it directly after changing speaker
        parameters when no message loop is running (e.g. in tests). */
    void refreshTriangulation();

private:
    void timerCallback() override { refreshTriangulation(); }
    void setParameterValue (const juce::String& id, float realValue);

    juce::AudioProcessorValueTreeState apvts;

    std::array<std::atomic<float>*, tripan::kMaxChannels> srcX {}, srcY {}, spkX {}, spkY {};
    std::atomic<float>* numSourcesParam = nullptr;
    std::atomic<float>* numSpeakersParam = nullptr;

    // Message thread -> audio thread
    tripan::TriangulationExchange exchange;
    std::vector<tripan::Vec2> publishedSpeakers; // message thread only
    bool havePublished = false;                  // message thread only

    // Audio thread only
    std::array<tripan::SourceGains, tripan::kMaxChannels> prevGains {};
    std::atomic<bool> restartRamp { true }; // set from any thread: next block starts at its target gains
    juce::AudioBuffer<float> inputCopy;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriPanAudioProcessor)
};
