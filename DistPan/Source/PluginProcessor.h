// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginProcessor.h
    @brief DistPan's audio processor: speaker hand-over and the DBAP panning itself. */

#pragma once

#include "distpan/Dbap.h"
#include "tlos/Exchange.h"
#include "tlos/SpatialProcessor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

/** DistPan's audio processor.

    Pans n source channels (the plugin's inputs) to m speaker channels (its outputs) with distance-based
    amplitude panning: every speaker gets a share that falls off with its distance from the source (see
    distpan::computeGains). The parameters, channel counts, edit modes, groups and state come from
    tlos::SpatialProcessor; DistPan adds the rolloff and the blur. The speaker positions are handed to
    processBlock() through a tlos::Exchange, and gains are ramped linearly across each block. */
class DistPanAudioProcessor final : public tlos::SpatialProcessor
{
public:
    /** Creates the processor: any layout up to 64 channels in and out, and all parameters. */
    DistPanAudioProcessor();

    /** Resets the gain ramps so that the first block starts at its target gains. */
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;

    /** Nothing to release (nothing is allocated per run). */
    void releaseResources() override;

    /** Pans the input channels to the output channels. Real-time safe: no allocation, locks or I/O. */
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** Creates the editor (see DistPanAudioProcessorEditor). */
    juce::AudioProcessorEditor* createEditor() override;

    /** @return The plugin's name. */
    const juce::String getName() const override { return JucePlugin_Name; }

    /** Message thread. Publishes the speaker positions to the audio thread if they or their number changed.
        Called by a timer; also call it directly after changing speaker parameters when no message loop is
        running (e.g. in tests). */
    void refreshLayout() override;

    /** @return The current rolloff and blur. Safe to call from any thread. */
    distpan::DbapSettings getSettings() const noexcept;

protected:
    /** Starts the next block from the restored positions without a ramp, and republishes the speakers. */
    void stateRestored() override;

private:
    /** The speakers in use at one moment, handed to the audio thread. Never changed after publishing. */
    struct SpeakerSet
    {
        std::vector<tlos::Vec2> positions; ///< Positions in metres.
        std::uint64_t id = 0;              ///< Distinguishes this set from all earlier ones (see GainKey).
    };

    /** One row of gains per source, one entry per speaker. */
    using GainMatrix = std::array<std::array<float, tlos::kMaxChannels>, tlos::kMaxChannels>;

    // Message thread -> audio thread
    tlos::Exchange<SpeakerSet> exchange;       ///< Hands speaker sets to the audio thread.
    std::vector<tlos::Vec2> publishedSpeakers; ///< Speakers of the last published set (message thread only).
    bool havePublished = false;                ///< Whether anything has been published (message thread only).
    std::uint64_t nextSetId = 1;               ///< Id of the next published set (message thread only).

    std::atomic<float>* rolloffParam = nullptr; ///< Raw value of "Rolloff".
    std::atomic<float>* blurParam = nullptr;    ///< Raw value of "Blur".

    // Audio thread only
    GainMatrix prevGains {};   ///< Gains at the end of the previous block.
    GainMatrix targetGains {}; ///< Gains wanted at the end of this block.
    int prevUsedSpeakers = 0;  ///< Speakers in use in the previous block.

    /** What prevGains[s] was computed from, so unchanged sources need no new computation. */
    struct GainKey
    {
        float x = 0.0f;              ///< Source x position.
        float y = 0.0f;              ///< Source y position.
        float spread = 0.0f;         ///< Source spread radius.
        float rolloff = 0.0f;        ///< Rolloff the gains were computed with.
        float blur = 0.0f;           ///< Blur the gains were computed with.
        std::uint64_t setId = 0;     ///< SpeakerSet::id the gains belong to.
        bool valid = false;          ///< False until gains have been computed.
    };
    std::array<GainKey, tlos::kMaxChannels> gainKeys {}; ///< Per source: what prevGains was computed from.
    std::atomic<bool> restartRamp { true }; ///< Set from any thread: the next block starts at its target gains.
    juce::AudioBuffer<float> inputCopy;     ///< Copy of the inputs, so that outputs may overwrite the buffer.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DistPanAudioProcessor)
};
