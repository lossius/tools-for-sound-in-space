// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file SpatialProcessor.h
    @brief Base class of the audio processors: parameters, channel counts, edit modes, groups and state. */

#pragma once

#include "tlos/Groups.h"
#include "tlos/Limits.h"
#include "tlos/OscControl.h"
#include "tlos/Parameters.h"
#include "tlos/Speakers.h"
#include "tlos/Vec2.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <optional>
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

/** A parameter of the panning method that every speaker has (DistPan: the weight). The speaker table shows it
    as a column, and the speaker files carry it in an attribute. */
struct SpeakerColumn
{
    juce::String title;                           ///< Header of the table column, for example "Weight".
    juce::String attribute;                       ///< Name of the attribute in speaker files, for example "weight".
    std::function<juce::String (int)> idFor;      ///< Parameter ID of speaker `index` (zero-based).
    double minimum = 0.0;                         ///< Smallest value.
    double maximum = 1.0;                         ///< Largest value.
    double defaultValue = 1.0;                    ///< The default value of the parameter.
    int decimals = 2;                             ///< Decimals shown in the table.
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
        @param extraParameters  Parameters that belong to the plugin's panning method: global ones (for example a rolloff) are
                                listed with the shared global parameters, before everything for sources and speakers; per-speaker
                                ones (for example a weight) with the speaker's own parameters. See params::createLayout().
        @param virtualSpeakerParameters  Whether the plugin has virtual speakers, and so the parameters for their mode,
                                number and positions. */
    SpatialProcessor (const BusesProperties& buses, const juce::String& stateType,
                      params::PluginParameters extraParameters = {},
                      bool virtualSpeakerParameters = false);

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

    /** Audio thread. Records the peak level of the first `numChannels` channels of `buffer` as input levels (call before the panning).
        Real-time safe. */
    void recordInputLevels (const juce::AudioBuffer<float>& buffer, int numChannels) noexcept;

    /** Audio thread. Records the peak level of the first `numChannels` channels of `buffer` as output levels (call after the panning).
        Real-time safe. */
    void recordOutputLevels (const juce::AudioBuffer<float>& buffer, int numChannels) noexcept;

    /** Message thread. The highest absolute sample value of source `index` (zero-based) since the last call, then forgets it. */
    float takeInputPeak (int index) noexcept { return inputPeaks[(size_t) index].exchange (0.0f); }

    /** Message thread. The highest absolute sample value of speaker `index` (zero-based) since the last call, then forgets it. */
    float takeOutputPeak (int index) noexcept { return outputPeaks[(size_t) index].exchange (0.0f); }

    /** Message thread. Whether speaker `index` (zero-based) is excluded from the panning: the panning is
        computed as if it did not exist (use this for an LFE channel or a broken speaker). Unlike mute, the
        others then share its signal. A host parameter ("Speaker N Exclude"), so the host's undo covers it. */
    bool isSpeakerExcluded (int index) const;
    /** Excludes or includes a speaker, and calls refreshLayout(). */
    void setSpeakerExcluded (int index, bool excluded);
    /** Sets the positions of several speakers at once: all x and y parameters that differ are written inside change gestures
        that are begun together and ended together (one step for the host's undo), then refreshLayout() is called. Positions are
        limited to +-params::kCoordRange. Independent of the Edit/View menus, like the tables and imports.
        @param channels   Zero-based speaker numbers.
        @param positions  The new position of each, in metres (same length as `channels`; the rest is ignored). */
    void setSpeakerPositions (const std::vector<int>& channels, const std::vector<Vec2>& positions);
    /** @return The excluded speakers (indexed by zero-based speaker number, also those not in use). */
    std::array<bool, kMaxChannels> getExcludedSpeakers() const;
    /** Replaces all exclusions at once, and calls refreshLayout(). */
    void setExcludedSpeakers (const std::array<bool, kMaxChannels>& excluded);
    /** @return The speakers in use that are not excluded, with the channel of each. */
    IncludedSpeakers getIncludedSpeakers() const;

    /** Message thread. The room: half the width of the square, in metres, that the pad shows and keeps sources
        and speakers in when they are dragged (default 10, so +-10 m). Positions outside it are still possible (the
        parameters, tables and files allow +-100 m) and sound as usual. Saved with the state, not a host parameter. */
    double getRoomHalfExtent() const;
    /** Sets the room half extent, limited to tlos::params::kMinRoomHalf .. kMaxRoomHalf. */
    void setRoomHalfExtent (double metres);

    /** How positions are shown and typed in the tables: x / y in metres, or azimuth (degrees, counter-clockwise
        from the front) and distance (metres). A display preference only: the host parameters are always x and y. */
    enum class CoordinateMode
    {
        Cartesian,   ///< X and Y in metres.
        Polar        ///< Azimuth in degrees and distance in metres.
    };

    /** Message thread. The coordinate mode of the tables. Saved with the state (not a host parameter); until
        the user chooses, it is defaultCoordinateMode(). */
    CoordinateMode getCoordinateMode() const;
    /** Sets the coordinate mode. */
    void setCoordinateMode (CoordinateMode mode);
    /** @return The mode a new instance starts in. Plugins whose positions are more naturally angles (VBAP) override it. */
    virtual CoordinateMode defaultCoordinateMode() const { return CoordinateMode::Cartesian; }

    /** Message thread. Places the active sources and/or speakers on rings (speakers on half the room half extent,
        5 m in the default room; sources on a quarter of it; first speaker at front). */
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

    /** Message thread. Source groups: sources in a group are moved together in the editor. The assignment is kept
        in host parameters ("Source N Group": 0 = none, 1 to 32), so it is saved with the project and the host's undo
        covers it.
        @return Which sources are grouped. */
    SourceGroups getSourceGroups() const;
    /** Replaces the source groups: writes the group parameters that differ, in one gesture. */
    void setSourceGroups (const SourceGroups&);

    /** Message thread. Width of the editor window in pixels (0 if never set); saved with the state
        so that the chosen GUI size is restored. */
    int getEditorWidth() const;

    /** Remembers the editor width (saved with the state). Message thread. */
    void setEditorWidth (int width);

    /** The most virtual speakers in the manual list. */
    static constexpr int kMaxVirtualSpeakers = params::kMaxVirtualSpeakers;

    /** @return The per-speaker parameter of the panning method, if the plugin has one (the default is none). */
    virtual std::optional<SpeakerColumn> getSpeakerColumn() const { return std::nullopt; }

    /** @return The plugin's own parameters that can be set over OSC, with their addresses (at most 4; the default is
        none). DistPan: /dbap/rolloff and /dbap/blur. */
    virtual std::vector<OscParameter> getOscParameters() const { return {}; }

    /** @return The OSC settings (saved with the state; everything is off in a new session). */
    OscSettings getOscSettings() const;
    /** Applies new OSC settings: opens or closes the port and the sending socket, and saves them with the state. */
    void setOscSettings (const OscSettings& settings);
    /** @return What OSC is doing (see OscControl::getStatus()). */
    juce::String getOscStatus() const;
    /** @return The OSC control (the settings panel and the tests use it). */
    OscControl& getOscControl() noexcept { return *osc; }

    /** @return Whether the plugin uses virtual (imaginary) speakers (it was created with their parameters). If not
        (the default), the editor has no menu for them and getVirtualMode() is always Off. */
    virtual bool supportsVirtualSpeakers() const noexcept { return hasVirtualParameters; }

    /** @return The current virtual speaker mode (a host parameter, "Virtual Speakers"). */
    VirtualMode getVirtualMode() const;
    /** @return Whether the user can currently add, move and delete virtual speakers by hand: the mode is
        Manual and the speakers are set to Edit. */
    bool canEditVirtualSpeakers() const;
    /** Changes the virtual speaker mode and calls refreshLayout(). Switching from Auto to Manual with an
        empty manual list starts the list from the automatic speakers that are at least half in effect. */
    void setVirtualMode (VirtualMode);
    /** @return The user's own virtual speaker positions (used in Manual mode), in metres. */
    std::vector<Vec2> getManualVirtualSpeakers() const;
    /** Replaces the manual list (at most #kMaxVirtualSpeakers, coordinates limited to the parameter range): the
        number of virtual speakers and the positions are host parameters, and the ones that differ are written.
        @param list         The new list.
        @param ownGesture   Whether to wrap the change in change gestures (what the host's undo needs). Pass false while a
                            drag is in progress, which has its own gesture (see beginVirtualSpeakerGesture()). */
    void setManualVirtualSpeakers (const std::vector<Vec2>& list, bool ownGesture = true);
    /** Starts a change gesture for the position of manual virtual speaker `index` (zero-based), for a drag. */
    void beginVirtualSpeakerGesture (int index);
    /** Ends the change gesture started by beginVirtualSpeakerGesture(). */
    void endVirtualSpeakerGesture (int index);
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
    std::array<std::atomic<float>*, kMaxChannels> srcGroup {};  ///< Raw values of the source group numbers.
    std::array<std::atomic<float>*, kMaxChannels> spkExclude {}; ///< Raw values of the speaker exclude switches (0 or 1).
    std::array<std::atomic<float>*, kMaxVirtualSpeakers> virtualX {};  ///< Raw values of the manual virtual speaker x positions (plugins with virtual speakers).
    std::array<std::atomic<float>*, kMaxVirtualSpeakers> virtualY {};  ///< Raw values of the manual virtual speaker y positions.
    std::atomic<float>* virtualModeParam = nullptr;  ///< Raw value of the virtual speaker mode (0 Auto, 1 Manual, 2 Off).
    std::atomic<float>* virtualCountParam = nullptr; ///< Raw value of the number of manual virtual speakers.
    bool hasVirtualParameters = false;               ///< Whether the plugin has virtual speakers.
    std::atomic<float>* numSourcesParam = nullptr;  ///< Raw value of "Number of Sources".
    std::atomic<float>* numSpeakersParam = nullptr; ///< Raw value of "Number of Speakers".
    std::atomic<float>* outsideRolloffParam = nullptr; ///< Raw value of "Outside Rolloff".

private:
    std::array<std::atomic<float>, kMaxChannels> inputPeaks {};   ///< Peak since the last takeInputPeak(), per source.
    std::array<std::atomic<float>, kMaxChannels> outputPeaks {};  ///< Peak since the last takeOutputPeak(), per speaker.

    /** Timer callback (30 Hz, message thread): calls refreshLayout(). */
    void timerCallback() override { refreshLayout(); }
    /** Sets parameters to real values: only those that differ are written, and with `gestures` all inside change
        gestures that are begun together and ended together (one step for the host's undo).
        @param changes   Parameter IDs and their new real values.
        @param gestures  Whether to wrap the writes in change gestures. */
    void writeParameters (const std::vector<std::pair<juce::String, float>>& changes, bool gestures = true);
    /** Moves what older versions kept in the saved state (groups, exclusions, virtual speakers) into the parameters. */
    void migrateLegacyState();

    std::unique_ptr<OscControl> osc;   ///< OSC in and out.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialProcessor)
};
}
