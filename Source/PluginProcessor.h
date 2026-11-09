// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginProcessor.h
    @brief The audio processor: parameters, state, triangulation hand-over and the panning itself. */

#pragma once

#include "tripan/Gains.h"
#include "tripan/Groups.h"
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
    Auto = 1,   ///< Virtual speakers are found from the speaker layout.
    Manual = 2, ///< The user's own list of virtual speakers is used.
    Off = 3     ///< No virtual speakers.
};

/** TriPan's audio processor.

    Pans n source channels (the plugin's inputs) to m speaker channels (its outputs). The positions
    are host parameters; speakers are triangulated off the audio thread and handed to processBlock()
    through a tripan::LayoutExchange, and gains are ramped linearly across each block. */
class TriPanAudioProcessor final : public juce::AudioProcessor,
                                   private juce::Timer
{
public:
    /** Creates the processor: any layout up to 64 channels in and out, all parameters, and a 30 Hz timer
        that keeps the triangulation up to date. */
    TriPanAudioProcessor();

    /** Stops the timer. */
    ~TriPanAudioProcessor() override;

    /** Resets the gain ramps so that the first block starts at its target gains. */
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;

    /** Nothing to release (nothing is allocated per run). */
    void releaseResources() override;

    /** Accepts any layout with equal or different input and output counts, up to 64 channels each. */
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    /** Pans the input channels to the output channels. Real-time safe: no allocation, locks or I/O. */
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** Creates the editor (see TriPanAudioProcessorEditor). */
    juce::AudioProcessorEditor* createEditor() override;

    /** @return true: the plugin has an editor. */
    bool hasEditor() const override { return true; }

    /** @return The plugin's name. */
    const juce::String getName() const override { return JucePlugin_Name; }
    /** @return false: MIDI is not used. */
    bool acceptsMidi() const override { return false; }
    /** @return false: MIDI is not produced. */
    bool producesMidi() const override { return false; }
    /** @return false: this is an audio effect. */
    bool isMidiEffect() const override { return false; }
    /** @return 0: there is no tail. */
    double getTailLengthSeconds() const override { return 0.0; }

    /** @return 1: programs are not used, but hosts expect at least one. */
    int getNumPrograms() override { return 1; }
    /** @return 0 (the only program). */
    int getCurrentProgram() override { return 0; }
    /** Does nothing: there is only one program. */
    void setCurrentProgram (int) override {}
    /** @return An empty name. */
    const juce::String getProgramName (int) override { return {}; }
    /** Does nothing: there is only one program. */
    void changeProgramName (int, const juce::String&) override {}

    /** Saves the parameters and the extra state (edit modes, virtual speakers, editor width). */
    void getStateInformation (juce::MemoryBlock&) override;

    /** Restores what getStateInformation() saved (the pre-release `editMode` property is still read). */
    void setStateInformation (const void*, int) override;

    /** @return The parameter tree (all positions, spreads and counts). */
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
    /** @return The parameter tree (const). */
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept { return apvts; }

    /** Number of sources / speakers in use: the "Number of ..." parameter clamped to 1..channels of
        the track (0 if the track has no such channels). Safe to call from any thread. */
    int getActiveSources() const noexcept;
    /** @copydoc getActiveSources() */
    int getActiveSpeakers() const noexcept;

    /** Message thread. Places the active sources and/or speakers on rings (speakers on radius 5 m,
        sources on radius 2.5 m, first speaker at front). */
    void resetLayoutToRings (bool resetSources = true, bool resetSpeakers = true);

    /** Message thread. Whether the editor lets the user change the source / speaker positions
        (otherwise they are in view mode). Saved with the state. Defaults: sources can be edited,
        speakers are view only. */
    bool canEditSources() const;
    /** @copydoc canEditSources() */
    bool canEditSpeakers() const;
    /** Sets whether source positions can be edited in the editor. Message thread. */
    void setSourcesEditable (bool);
    /** Sets whether speaker positions can be edited in the editor. Message thread. */
    void setSpeakersEditable (bool);

    /** Message thread. Virtual speakers. They are not host parameters; the mode and the manual list
        are saved with the state. Switching from Auto to Manual with an empty manual list starts
        the manual list from the automatic speakers that are at least half in effect. */
    static constexpr int kMaxVirtualSpeakers = 64;

    /** @return The current virtual speaker mode. */
    VirtualMode getVirtualMode() const;
    /** @return Whether the user can currently add, move and delete virtual speakers by hand: the mode is
        Manual and the speakers are set to Edit. */
    bool canEditVirtualSpeakers() const;
    /** Changes the virtual speaker mode and republishes the layout. */
    void setVirtualMode (VirtualMode);
    /** @return The user's own virtual speaker positions (used in Manual mode), in metres. */
    std::vector<tripan::Vec2> getManualVirtualSpeakers() const;
    /** Replaces the manual list (at most #kMaxVirtualSpeakers, coordinates limited to +-10 m). */
    void setManualVirtualSpeakers (const std::vector<tripan::Vec2>&);
    /** Sets the mode and, if `manualList` is given, the manual list in one go (used when a speaker file
        is imported). Unlike setVirtualMode(), it never copies the automatic speakers into the list. */
    void setVirtualLayout (VirtualMode, const std::vector<tripan::Vec2>* manualList);

    /** The virtual speakers in effect (according to the mode), for the speakers currently in use. Manual
        ones have strength 1; automatic ones have a strength between 0 and 1 that follows the layout
        smoothly (see tripan::SpeakerLayout). */
    std::vector<tripan::VirtualSpeaker> getVirtualSpeakers() const;

    /** Message thread. Source groups: sources in a group are moved together in the editor. They are not
        host parameters; the assignment is saved with the state (and so survives closing the project).
        @return Which sources are grouped. */
    tripan::SourceGroups getSourceGroups() const;
    /** Replaces the source groups (saved with the state). */
    void setSourceGroups (const tripan::SourceGroups&);

    /** Message thread. Width of the editor window in pixels (0 if never set); saved with the state
        so that the chosen GUI size is restored. */
    int getEditorWidth() const;

    /** Remembers the editor width (saved with the state). Message thread. */
    void setEditorWidth (int width);

    /** Message thread. Rebuilds and publishes the triangulation if the active speaker positions
        or count changed. Called by a timer; also call it directly after changing speaker
        parameters when no message loop is running (e.g. in tests). */
    void refreshTriangulation();

private:
    /** Timer callback (30 Hz, message thread): picks up speaker changes. */
    void timerCallback() override { refreshTriangulation(); }
    /** Sets a parameter from a real value, inside a change gesture. */
    void setParameterValue (const juce::String& id, float realValue);
    /** Writes the manual virtual speaker list to the saved state. */
    void storeManualVirtualSpeakers (const std::vector<tripan::Vec2>&);

    juce::AudioProcessorValueTreeState apvts; ///< All parameters and the saved extra state.

    std::array<std::atomic<float>*, tripan::kMaxChannels> srcX {};      ///< Raw values of the source x parameters.
    std::array<std::atomic<float>*, tripan::kMaxChannels> srcY {};      ///< Raw values of the source y parameters.
    std::array<std::atomic<float>*, tripan::kMaxChannels> srcSpread {}; ///< Raw values of the source spread parameters.
    std::array<std::atomic<float>*, tripan::kMaxChannels> spkX {};      ///< Raw values of the speaker x parameters.
    std::array<std::atomic<float>*, tripan::kMaxChannels> spkY {};      ///< Raw values of the speaker y parameters.
    std::atomic<float>* numSourcesParam = nullptr;  ///< Raw value of "Number of Sources".
    std::atomic<float>* numSpeakersParam = nullptr; ///< Raw value of "Number of Speakers".

    // Message thread -> audio thread
    tripan::LayoutExchange exchange;                       ///< Hands layouts to the audio thread.
    std::vector<tripan::Vec2> publishedSpeakers;           ///< Speakers of the last published layout (message thread only).
    std::vector<tripan::VirtualSpeaker> publishedVirtual;  ///< Virtual speakers of the last published layout (message thread only).
    bool havePublished = false;                            ///< Whether anything has been published (message thread only).

    // Audio thread only
    std::array<tripan::SourceGains, tripan::kMaxChannels> prevGains {}; ///< Gains at the end of the previous block, per source.
    int prevUsedSpeakers = 0;                                           ///< Speakers in use in the previous block.

    /** What prevGains[s] was computed from, so unchanged sources need no new computation. */
    struct GainKey
    {
        float x = 0.0f;               ///< Source x position.
        float y = 0.0f;               ///< Source y position.
        float spread = 0.0f;          ///< Source spread radius.
        std::uint64_t layoutId = 0;   ///< SpeakerLayout::id() the gains belong to.
        bool valid = false;           ///< False until gains have been computed.
    };
    std::array<GainKey, tripan::kMaxChannels> gainKeys {}; ///< Per source: what prevGains was computed from.
    std::atomic<bool> restartRamp { true }; ///< Set from any thread: the next block starts at its target gains.
    juce::AudioBuffer<float> inputCopy;     ///< Copy of the inputs, so that outputs may overwrite the buffer.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriPanAudioProcessor)
};
