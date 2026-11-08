#pragma once

#include "tripan/Gains.h"
#include "tripan/Limits.h"
#include "tripan/LayoutExchange.h"
#include "tripan/VirtualSpeakers.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

/** Where the virtual (imaginary) speakers come from.
    Auto: found from the speaker layout by tripan::findVirtualSpeakers. Manual: the user's own list.
    Off: none, plain triangulation of the real speakers. */
enum class VirtualMode
{
    Auto = 1,
    Manual = 2,
    Off = 3
};

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
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept { return apvts; }

    /** Number of sources / speakers in use: the "Number of ..." parameter clamped to 1..channels of
        the track (0 if the track has no such channels). Safe to call from any thread. */
    int getActiveSources() const noexcept;
    int getActiveSpeakers() const noexcept;

    /** Message thread. Places the active sources and/or speakers on rings (speakers on radius 5 m,
        sources on radius 2.5 m, first speaker at front). */
    void resetLayoutToRings (bool resetSources = true, bool resetSpeakers = true);

    /** Message thread. Whether the editor lets the user change the source / speaker positions
        (otherwise they are in view mode). Saved with the state. Defaults: sources can be edited,
        speakers are view only. */
    bool canEditSources() const;
    bool canEditSpeakers() const;
    void setSourcesEditable (bool);
    void setSpeakersEditable (bool);

    /** Message thread. Virtual speakers. They are not host parameters; the mode and the manual list
        are saved with the state. Switching from Auto to Manual with an empty manual list starts
        the manual list from the automatic speakers that are at least half in effect. */
    static constexpr int kMaxVirtualSpeakers = 16;
    VirtualMode getVirtualMode() const;
    void setVirtualMode (VirtualMode);
    std::vector<tripan::Vec2> getManualVirtualSpeakers() const;
    void setManualVirtualSpeakers (const std::vector<tripan::Vec2>&);
    /** Sets the mode and, if `manualList` is given, the manual list in one go (used when a speaker file
        is imported). Unlike setVirtualMode(), it never copies the automatic speakers into the list. */
    void setVirtualLayout (VirtualMode, const std::vector<tripan::Vec2>* manualList);

    /** The virtual speakers in effect (according to the mode), for the speakers currently in use. Manual
        ones have strength 1; automatic ones have a strength between 0 and 1 that follows the layout
        smoothly (see tripan::SpeakerLayout). */
    std::vector<tripan::VirtualSpeaker> getVirtualSpeakers() const;

    /** Message thread. Width of the editor window in pixels (0 if never set); saved with the state
        so that the chosen GUI size is restored. */
    int getEditorWidth() const;
    void setEditorWidth (int width);

    /** Message thread. Rebuilds and publishes the triangulation if the active speaker positions
        or count changed. Called by a timer; also call it directly after changing speaker
        parameters when no message loop is running (e.g. in tests). */
    void refreshTriangulation();

private:
    void timerCallback() override { refreshTriangulation(); }
    void setParameterValue (const juce::String& id, float realValue);
    void storeManualVirtualSpeakers (const std::vector<tripan::Vec2>&);

    juce::AudioProcessorValueTreeState apvts;

    std::array<std::atomic<float>*, tripan::kMaxChannels> srcX {}, srcY {}, srcSpread {}, spkX {}, spkY {};
    std::atomic<float>* numSourcesParam = nullptr;
    std::atomic<float>* numSpeakersParam = nullptr;

    // Message thread -> audio thread
    tripan::LayoutExchange exchange;
    std::vector<tripan::Vec2> publishedSpeakers; // message thread only
    std::vector<tripan::VirtualSpeaker> publishedVirtual; // message thread only
    bool havePublished = false;                  // message thread only

    // Audio thread only
    std::array<tripan::SourceGains, tripan::kMaxChannels> prevGains {};
    int prevUsedSpeakers = 0;

    // What prevGains[s] was computed from, so unchanged sources need no new computation.
    struct GainKey
    {
        float x = 0.0f, y = 0.0f, spread = 0.0f;
        std::uint64_t layoutId = 0;
        bool valid = false;
    };
    std::array<GainKey, tripan::kMaxChannels> gainKeys {};
    std::atomic<bool> restartRamp { true }; // set from any thread: next block starts at its target gains
    juce::AudioBuffer<float> inputCopy;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriPanAudioProcessor)
};
