// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginProcessor.h
    @brief TriPan's audio processor: triangulation hand-over and the panning itself. */

#pragma once

#include "tlos/SpatialProcessor.h"
#include "tripan/Gains.h"
#include "tlos/Exchange.h"
#include "tripan/SpeakerLayout.h"
#include "tripan/VirtualSpeakers.h"

#include <array>
#include <atomic>
#include <vector>

/** TriPan's audio processor.

    Pans n source channels (the plugin's inputs) to m speaker channels (its outputs). The parameters,
    channel counts, edit modes, groups and state come from tlos::SpatialProcessor. Speakers are
    triangulated off the audio thread and handed to processBlock() through a tripan::LayoutExchange, and
    gains are ramped linearly across each block. */
class TriPanAudioProcessor final : public tlos::SpatialProcessor
{
public:
    /** Creates the processor: any layout up to 64 channels in and out, and all parameters. */
    TriPanAudioProcessor();

    /** Resets the gain ramps so that the first block starts at its target gains. */
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;

    /** Nothing to release (nothing is allocated per run). */
    void releaseResources() override;

    /** Pans the input channels to the output channels. Real-time safe: no allocation, locks or I/O. */
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** Creates the editor (see TriPanAudioProcessorEditor). */
    juce::AudioProcessorEditor* createEditor() override;

    /** @return The plugin's name. */
    const juce::String getName() const override { return JucePlugin_Name; }

    /** @return true: TriPan fills gaps in the speaker layout with virtual speakers. */
    bool supportsVirtualSpeakers() const noexcept override { return true; }

    /** Message thread. Rebuilds and publishes the triangulation if the active speaker positions
        or count changed. Called by a timer; also call it directly after changing speaker
        parameters when no message loop is running (e.g. in tests). */
    void refreshLayout() override;

protected:
    /** Starts the next block from the restored positions without a ramp, and republishes the triangulation. */
    void stateRestored() override;

    /** The automatic virtual speakers (see tripan::findVirtualSpeakers). */
    std::vector<tlos::VirtualSpeaker> findAutomaticVirtualSpeakers (const std::vector<tlos::Vec2>& speakers) const override;

private:
    /** What the audio thread gets from the message thread: the layout of the speakers that take part in the
        panning, and the output channel of each of them (they differ from the channel numbers when speakers are excluded). */
    struct LayoutSnapshot
    {
        /** Builds the layout of the included speakers and the virtual speakers. Allocates. */
        LayoutSnapshot (const tlos::IncludedSpeakers& speakers, const std::vector<tlos::VirtualSpeaker>& virtualSpeakers)
            : layout (speakers.positions, virtualSpeakers), channelOf (speakers.channelOf) {}

        tripan::SpeakerLayout layout;  ///< The triangulations (indices are positions in the included speakers).
        std::vector<int> channelOf;    ///< Output channel of each included speaker.
    };

    // Message thread -> audio thread
    tlos::Exchange<LayoutSnapshot> exchange;               ///< Hands layouts to the audio thread.
    tlos::IncludedSpeakers publishedSpeakers;              ///< Speakers of the last published layout (message thread only).
    std::vector<tlos::VirtualSpeaker> publishedVirtual;    ///< Virtual speakers of the last published layout (message thread only).
    bool havePublished = false;                            ///< Whether anything has been published (message thread only).

    // Audio thread only
    std::array<tripan::SourceGains, tlos::kMaxChannels> prevGains {}; ///< Gains at the end of the previous block, per source (with trim, mute and solo).
    std::array<tripan::SourceGains, tlos::kMaxChannels> panGains {};  ///< The panning gains of each source alone, per output channel: what the cache holds.
    int prevUsedSpeakers = 0;                                         ///< Speakers in use in the previous block.

    /** What prevGains[s] was computed from, so unchanged sources need no new computation. */
    struct GainKey
    {
        float x = 0.0f;               ///< Source x position.
        float y = 0.0f;               ///< Source y position.
        float spread = 0.0f;          ///< Source spread radius.
        float outside = 0.0f;         ///< Outside rolloff the gains were computed with.
        std::uint64_t layoutId = 0;   ///< SpeakerLayout::id() the gains belong to.
        bool valid = false;           ///< False until gains have been computed.
    };
    std::array<GainKey, tlos::kMaxChannels> gainKeys {}; ///< Per source: what prevGains was computed from.
    std::atomic<bool> restartRamp { true }; ///< Set from any thread: the next block starts at its target gains.
    juce::AudioBuffer<float> inputCopy;     ///< Copy of the inputs, so that outputs may overwrite the buffer.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriPanAudioProcessor)
};
