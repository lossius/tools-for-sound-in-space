// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginProcessor.h
    @brief ViMiC's audio processor: the virtual microphones' gains and delays and the delay lines that render them. */

#pragma once

#include "ViMiCParameters.h"
#include "tlos/Exchange.h"
#include "tlos/SpatialProcessor.h"
#include "vimic/DelayLine.h"
#include "vimic/Path.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

/** ViMiC's audio processor.

    The sources (the plugin's inputs) are recorded by virtual microphones (its outputs): for every source and microphone the direct sound has a gain
    (distance law times the microphone's directivity pattern) and a delay (the time of flight), see vimic::computeDirectPath(). Each source's signal is
    kept in a delay line that every microphone reads at its own, smoothly changing delay with an interpolated tap (vimic::DelayLine), so that a moving
    source gives a Doppler shift in the Absolute delay mode and none in the Relative one. The parameters, channel counts, edit modes, groups and
    state come from tlos::SpatialProcessor; ViMiC adds the model settings and the yaw and pattern of every microphone. The microphone positions are
    handed to processBlock() through a tlos::Exchange, and gains and delays are ramped linearly across each block.

    The plugin reports a latency of 2 samples (the shortest delay the interpolation allows is added to every path). */
class ViMiCAudioProcessor final : public tlos::SpatialProcessor
{
public:
    /** Creates the processor: any layout up to 64 channels in and out, and all parameters. */
    ViMiCAudioProcessor();

    /** Allocates the delay lines (one per input channel, one second long) and resets the ramps. */
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;

    /** Nothing to release. */
    void releaseResources() override;

    /** Renders the microphone signals. Real-time safe: no allocation, locks or I/O. */
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** Creates the editor (see ViMiCAudioProcessorEditor). */
    juce::AudioProcessorEditor* createEditor() override;

    /** @return The plugin's name. */
    const juce::String getName() const override { return JucePlugin_Name; }

    /** Message thread. Publishes the microphone positions to the audio thread if they or their number changed. */
    void refreshLayout() override;

    /** @return The addresses /vimic/... of the model settings. */
    std::vector<tlos::OscParameter> getOscParameters() const override;

    /** @return The yaw and the pattern columns of the microphone table and files. */
    std::vector<tlos::SpeakerColumn> getSpeakerColumns() const override;

    /** @return The yaw and the pattern columns of the source table and files. */
    std::vector<tlos::SpeakerColumn> getSourceColumns() const override;

    /** @return "Microphone". */
    juce::String getOutputNoun() const override { return "Microphone"; }
    /** @return false: ViMiC has no use for the spread of a source. */
    bool usesSpread() const override { return false; }
    /** @return false: the level of ViMiC follows its distance law, not the outside rolloff. */
    bool usesOutsideRolloff() const override { return false; }
    /** @return Polar: positions as seen from the listener, like the microphones' yaw. */
    CoordinateMode defaultCoordinateMode() const override { return CoordinateMode::Polar; }

    /** @return The settings of the model as they are now. Safe to call from any thread. */
    vimic::PathSettings getPathSettings() const noexcept;

    /** @return The delay mode as it is now. Safe to call from any thread. */
    vimic::params::DelayMode getDelayMode() const noexcept;

    /** @return Source `index` (zero-based) as it is now: position, yaw and radiation pattern. Safe to call from any thread. */
    vimic::Source getSource (int index) const noexcept;

    /** @return The microphones for the speakers in `speakers` (their positions, with the yaw and pattern of each). Safe to call from any thread. */
    std::vector<vimic::Microphone> getMicrophones (const tlos::IncludedSpeakers& speakers) const;

protected:
    /** Starts the next block from the restored positions without a ramp, and republishes the microphones. */
    void stateRestored() override;

    /** Points every microphone outwards (its yaw becomes the azimuth of its position) and every source towards the centre. */
    void layoutWasReset (bool sources, bool speakers) override;

private:
    /** The microphones in use at one moment, handed to the audio thread. Never changed after publishing. */
    struct MicSet
    {
        std::vector<tlos::Vec2> positions;   ///< Positions in metres.
        std::vector<int> channelOf;          ///< Output channel of each microphone (they differ from the channel numbers when microphones are excluded).
        std::uint64_t id = 0;                ///< Distinguishes this set from all earlier ones.
    };

    /** One value per source and output channel. */
    using Matrix = std::array<std::array<float, tlos::kMaxChannels>, tlos::kMaxChannels>;

    /** Everything the gains and delays depend on apart from the source positions (compared each block; a change recomputes every source). */
    struct Settings
    {
        std::array<float, vimic::params::GlobalCount> globals {};   ///< The model parameters.
        std::array<float, tlos::kMaxChannels> yaw {};            ///< Yaw of each microphone in the set (degrees).
        std::array<float, tlos::kMaxChannels> pattern {};        ///< Pattern of each microphone in the set.
        std::uint64_t setId = 0;                                 ///< Id of the microphone set.
        double sampleRate = 0.0;                                 ///< The sample rate the delays were computed for.
        bool valid = false;                                      ///< False until set once.
    };

    // Message thread -> audio thread
    tlos::Exchange<MicSet> exchange;           ///< Hands microphone sets to the audio thread.
    tlos::IncludedSpeakers publishedMics;      ///< Microphones of the last published set (message thread only).
    bool havePublished = false;                ///< Whether anything has been published (message thread only).
    std::uint64_t nextSetId = 1;               ///< Id of the next published set (message thread only).

    std::array<std::atomic<float>*, vimic::params::GlobalCount> globalParam {};   ///< Raw values of the model parameters.
    std::array<std::atomic<float>*, tlos::kMaxChannels> srcYawParam {};   ///< Raw values of the source yaws.
    std::array<std::atomic<float>*, tlos::kMaxChannels> srcPatternParam {};   ///< Raw values of the source patterns.
    std::array<std::atomic<float>*, tlos::kMaxChannels> yawParam {};      ///< Raw values of the microphone yaws.
    std::array<std::atomic<float>*, tlos::kMaxChannels> patternParam {};  ///< Raw values of the microphone patterns.

    // Audio thread only
    double sampleRate = 48000.0;               ///< The sample rate from prepareToPlay().
    std::vector<vimic::DelayLine> lines;       ///< One delay line per input channel.
    Matrix prevGains {};      ///< Gains at the end of the previous block (with trim, mute and solo).
    Matrix panGains {};       ///< The gains of the paths alone: what the cache holds.
    Matrix targetGains {};    ///< Gains wanted at the end of this block (with trim, mute, solo and the gains of the direct sound and the output).
    Matrix prevDelays {};     ///< Delays in samples at the end of the previous block.
    Matrix delays {};         ///< Delays in samples of the paths alone: what the cache holds.
    Matrix targetDelays {};   ///< Delays wanted at the end of this block.
    int prevUsedSpeakers = 0;                  ///< Microphones in use in the previous block.
    Settings usedSettings;                     ///< What the cached paths were computed with.

    /** What a source's cached paths were computed from. */
    struct PathKey
    {
        float x = 0.0f;            ///< Source x.
        float y = 0.0f;            ///< Source y.
        float yaw = 0.0f;          ///< Source yaw in degrees.
        float pattern = 1.0f;      ///< Source pattern.
        bool valid = false;        ///< False until computed.
    };
    std::array<PathKey, tlos::kMaxChannels> pathKeys {};   ///< Per source: what its paths were computed from.
    std::atomic<bool> restartRamp { true };    ///< Set from any thread: the next block starts at its target gains and delays.
    juce::AudioBuffer<float> inputCopy;        ///< Copy of the inputs, so that outputs may overwrite the buffer.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViMiCAudioProcessor)
};
