// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file SpatialProcessor.h
    @brief Base class of the audio processors: parameters, channel counts, edit modes, groups and state. */

#pragma once

#include "tlos/Groups.h"
#include "tlos/Limits.h"
#include "tlos/Speakers.h"
#include "tlos/Vec2.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace tlos
{
/** Where the virtual (imaginary) speakers come from.
    Auto: found from the speaker layout by the plugin. Manual: the user's own list.
    Off: none, only the real speakers are used. */
enum class VirtualMode
{
    Auto = 1,   ///< Virtual speakers are found from the speaker layout.
    Manual = 2, ///< The user's own list of virtual speakers is used.
    Off = 3     ///< No virtual speakers.
};

/** The gain of every source and every speaker that comes from its trim, mute and solo (linear, 0 = silent). */
struct ChannelGains
{
    std::array<float, kMaxChannels> sources {};   ///< Gain of each source, applied to its signal before the panning.
    std::array<float, kMaxChannels> speakers {};  ///< Gain of each speaker, applied to its output after the panning.
};

/** What every plugin in this repository has in common, so that they all look and behave alike.

    Sources are the plugin's input channels and speakers its output channels, both placed in one plane.
    This class owns the host parameters (see tlos::params: x and y of every source and speaker, the
    spread of every source and the two channel counts), and what is saved with the state besides them:
    which kinds of positions the editor may change, the source groups, the virtual speaker setting and
    the editor width. A plugin derives from it, adds the panning itself (processBlock()) and decides
    what to do when the layout changes (refreshLayout()).

    Threads: everything that is not marked otherwise is for the message thread. */
class SpatialProcessor : public juce::AudioProcessor,
                         private juce::Timer
{
public:
    /** Creates the processor with one input bus ("Sources") and one output bus ("Speakers"), all
        parameters, and a 30 Hz timer that calls refreshLayout().
        @param buses            The buses (use stereo defaults; hosts change them).
        @param stateType        The tag of the saved state. Keep it unchanged between versions of a plugin: it
                                is how an old session is recognised.
        @param extraParameters  Parameters that belong to the plugin's panning method (for example a rolloff).
                                They come after the shared ones in the host's list. */
    SpatialProcessor (const BusesProperties& buses, const juce::String& stateType,
                      std::vector<std::unique_ptr<juce::RangedAudioParameter>> extraParameters = {});

    /** Stops the timer. */
    ~SpatialProcessor() override;

    /** Accepts any layout with equal or different input and output counts, up to 64 channels each:
        discrete, standard, or ambisonic (orders 1-7 are used only as channel-count carriers). */
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    /** @return true: the plugin has an editor. */
    bool hasEditor() const override { return true; }

    /** @return false: MIDI is not used. */
    bool acceptsMidi() const override { return false; }
    /** @return false: MIDI is not produced. */
    bool producesMidi() const override { return false; }
    /** @return false: these are audio effects. */
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

    /** Saves the parameters and the extra state (edit modes, groups, virtual speakers, editor width). */
    void getStateInformation (juce::MemoryBlock&) override;

    /** Restores what getStateInformation() saved (the pre-release `editMode` property is still read),
        then calls stateRestored(). */
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

    /** @return The outside rolloff in dB (the "Outside Rolloff" parameter): how fast the level falls for a source
        outside the speaker layout, see tlos::outsideFactor(). 0 = no falloff. Safe to call from any thread. */
    double getOutsideRolloff() const noexcept;

    /** @return The positions of the speakers in use, in metres (all of them, also the excluded ones). */
    std::vector<Vec2> getActiveSpeakerPositions() const;

    /** Fills in the gain of every source and speaker from its trim, mute and solo parameters:
        trim in dB as a linear gain; 0 if muted; 0 if some source (or speaker) in use is soloed and this one is not.
        Mute wins over solo. Sources and speakers are soloed separately: a soloed source does not silence speakers.
        Entries beyond the sources or speakers in use are 0. Real-time safe; safe to call from any thread.
        @param gains  Receives the result. */
    void getChannelGains (ChannelGains& gains) const noexcept;

    /** Message thread. Whether speaker `index` (zero-based) is excluded from the panning: the panning is
        computed as if it did not exist (use this for an LFE channel or a broken speaker). Unlike mute, the
        others then share its signal. Saved with the state, not a host parameter. */
    bool isSpeakerExcluded (int index) const;
    /** Excludes or includes a speaker, and calls refreshLayout(). */
    void setSpeakerExcluded (int index, bool excluded);
    /** @return The excluded speakers (indexed by zero-based speaker number, also those not in use). */
    std::array<bool, kMaxChannels> getExcludedSpeakers() const;
    /** Replaces all exclusions at once, and calls refreshLayout(). */
    void setExcludedSpeakers (const std::array<bool, kMaxChannels>& excluded);
    /** @return The speakers in use that are not excluded, with the channel of each. */
    IncludedSpeakers getIncludedSpeakers() const;

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

    /** Message thread. Source groups: sources in a group are moved together in the editor. They are not
        host parameters; the assignment is saved with the state (and so survives closing the project).
        @return Which sources are grouped. */
    SourceGroups getSourceGroups() const;
    /** Replaces the source groups (saved with the state). */
    void setSourceGroups (const SourceGroups&);

    /** Message thread. Width of the editor window in pixels (0 if never set); saved with the state
        so that the chosen GUI size is restored. */
    int getEditorWidth() const;

    /** Remembers the editor width (saved with the state). Message thread. */
    void setEditorWidth (int width);

    /** The most virtual speakers in the manual list. */
    static constexpr int kMaxVirtualSpeakers = 64;

    /** @return Whether the plugin uses virtual (imaginary) speakers. If not (the default), the editor has
        no menu for them and getVirtualMode() is always Off. */
    virtual bool supportsVirtualSpeakers() const noexcept { return false; }

    /** @return The current virtual speaker mode (saved with the state, not a host parameter). */
    VirtualMode getVirtualMode() const;
    /** @return Whether the user can currently add, move and delete virtual speakers by hand: the mode is
        Manual and the speakers are set to Edit. */
    bool canEditVirtualSpeakers() const;
    /** Changes the virtual speaker mode and calls refreshLayout(). Switching from Auto to Manual with an
        empty manual list starts the list from the automatic speakers that are at least half in effect. */
    void setVirtualMode (VirtualMode);
    /** @return The user's own virtual speaker positions (used in Manual mode), in metres. */
    std::vector<Vec2> getManualVirtualSpeakers() const;
    /** Replaces the manual list (at most #kMaxVirtualSpeakers, coordinates limited to +-10 m). */
    void setManualVirtualSpeakers (const std::vector<Vec2>&);
    /** Sets the mode and, if `manualList` is given, the manual list in one go (used when a speaker file
        is imported). Unlike setVirtualMode(), it never copies the automatic speakers into the list. */
    void setVirtualLayout (VirtualMode, const std::vector<Vec2>* manualList);

    /** The virtual speakers in effect (according to the mode), for the speakers currently in use. Manual
        ones have strength 1; automatic ones come from findAutomaticVirtualSpeakers(). */
    std::vector<VirtualSpeaker> getVirtualSpeakers() const;

    /** Message thread. Tells the plugin that positions or counts may have changed, so that it can update
        what it hands to the audio thread (for TriPan: the triangulation). Called by a timer, after the
        layout is reset, and by the editor and the import after they changed parameters; call it directly
        after changing parameters when no message loop is running (e.g. in tests). The default does nothing. */
    virtual void refreshLayout() {}

protected:
    /** Called after a saved state has been restored: the default calls refreshLayout(). */
    virtual void stateRestored() { refreshLayout(); }

    /** The virtual speakers the plugin wants in Auto mode, for the given speakers in use. The default is none. */
    virtual std::vector<VirtualSpeaker> findAutomaticVirtualSpeakers (const std::vector<Vec2>& speakers) const;

    /** Call from prepareToPlay(): on the very first run places the active channels on rings (a restored
        session carries the flag in its state and is left untouched), otherwise calls refreshLayout(). */
    void initialiseLayoutIfFirstRun();

    /** Sets a parameter from a real value, inside a change gesture. */
    void setParameterValue (const juce::String& id, float realValue);

    juce::AudioProcessorValueTreeState apvts; ///< All parameters and the saved extra state.

    std::array<std::atomic<float>*, kMaxChannels> srcX {};      ///< Raw values of the source x parameters.
    std::array<std::atomic<float>*, kMaxChannels> srcY {};      ///< Raw values of the source y parameters.
    std::array<std::atomic<float>*, kMaxChannels> srcSpread {}; ///< Raw values of the source spread parameters.
    std::array<std::atomic<float>*, kMaxChannels> spkX {};      ///< Raw values of the speaker x parameters.
    std::array<std::atomic<float>*, kMaxChannels> spkY {};      ///< Raw values of the speaker y parameters.
    std::array<std::atomic<float>*, kMaxChannels> srcTrim {};   ///< Raw values of the source trim parameters (dB).
    std::array<std::atomic<float>*, kMaxChannels> srcMute {};   ///< Raw values of the source mute switches (0 or 1).
    std::array<std::atomic<float>*, kMaxChannels> srcSolo {};   ///< Raw values of the source solo switches (0 or 1).
    std::array<std::atomic<float>*, kMaxChannels> spkTrim {};   ///< Raw values of the speaker trim parameters (dB).
    std::array<std::atomic<float>*, kMaxChannels> spkMute {};   ///< Raw values of the speaker mute switches (0 or 1).
    std::array<std::atomic<float>*, kMaxChannels> spkSolo {};   ///< Raw values of the speaker solo switches (0 or 1).
    std::atomic<float>* numSourcesParam = nullptr;  ///< Raw value of "Number of Sources".
    std::atomic<float>* numSpeakersParam = nullptr; ///< Raw value of "Number of Speakers".
    std::atomic<float>* outsideRolloffParam = nullptr; ///< Raw value of "Outside Rolloff".

private:
    /** Timer callback (30 Hz, message thread): calls refreshLayout(). */
    void timerCallback() override { refreshLayout(); }
    /** Writes the manual virtual speaker list to the saved state. */
    void storeManualVirtualSpeakers (const std::vector<Vec2>&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialProcessor)
};
}
