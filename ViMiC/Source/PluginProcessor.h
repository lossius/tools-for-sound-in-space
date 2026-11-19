// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginProcessor.h
    @brief ViMiC's audio processor: the virtual microphones' gains and delays for the direct sound and the early reflections, and the delay lines that render them. */

#pragma once

#include "ViMiCParameters.h"
#include "tlos/Exchange.h"
#include "tlos/SpatialProcessor.h"
#include "vimic/DelayLine.h"
#include "vimic/Filter.h"
#include "vimic/Path.h"
#include "vimic/Reflections.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

/** ViMiC's audio processor.

    The sources (the plugin's inputs) are recorded by virtual microphones (its outputs): for every source and microphone the direct sound has a gain
    (distance law times the microphone's and the source's directivity patterns) and a delay (the time of flight), see vimic::computeDirectPath(). With the
    reflections switched on, a shoebox room adds, for every source and microphone, the paths of its images in the walls (1st order: 6, 2nd order: 18), each with
    its own gain and delay (vimic::computeImagePath()) and coloured by the absorption of the walls it hits (a filter that depends only on which walls, so that it is
    applied once per source and wall sequence, before the delay: see vimic::PathFilter). Each source's signal is kept in a delay line (the direct sound) and the filtered
    copies in the reflection bank; every microphone reads them at its own, smoothly changing delay with an interpolated tap (vimic::DelayLine), so that a moving source gives a
    Doppler shift in the Absolute delay mode and none in the Relative one. The parameters, channel counts, edit modes, groups and state come from tlos::SpatialProcessor; ViMiC adds
    the model, the room and the yaw and pattern of every microphone and source. The microphone positions and the reflection bank (the memory for the filtered copies, which depends on the
    room) are handed to processBlock() through tlos::Exchange objects, and gains and delays are ramped linearly across each block.

    The plugin reports a latency of 2 samples (the shortest delay the interpolation allows is added to every path). */
class ViMiCAudioProcessor final : public tlos::SpatialProcessor
{
public:
    /** Creates the processor: any layout up to 64 channels in and out, and all parameters. */
    ViMiCAudioProcessor();

    /** Allocates the delay lines of the direct sound (one per input channel, one second long) and resets the ramps; the reflection bank follows from the timer. */
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;

    /** Nothing to release. */
    void releaseResources() override;

    /** Renders the microphone signals. Real-time safe: no allocation, locks or I/O. */
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** Creates the editor (see ViMiCAudioProcessorEditor). */
    juce::AudioProcessorEditor* createEditor() override;

    /** @return The plugin's name. */
    const juce::String getName() const override { return JucePlugin_Name; }

    /** Message thread. Publishes the microphone positions to the audio thread if they or their number changed, and makes the reflection bank when the
        reflections are on and there is none that is large enough for the room. */
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
    /** @return "Display": the first section of the settings window is about the pad, not about the room (which has its own section). */
    juce::String getDisplaySectionTitle() const override { return "Pad"; }
    /** @return "Pad size": the size of the area the pad shows, not the room of the reflections. */
    juce::String getPadSizeCaption() const override { return "Pad size"; }
    /** @return What the pad size is, and that the room is set below. */
    juce::String getPadSizeTooltip() const override
    {
        return "How far the pad reaches: half the width of the area it shows, in metres. Sources and microphones stop at its edge when dragged "
               "(positions beyond it are still possible from the tables, files and the host, up to 100 m). The room of the reflections is set below, under Room and early reflections.";
    }

    /** @return The settings of the model as they are now. Safe to call from any thread. */
    vimic::PathSettings getPathSettings() const noexcept;

    /** @return The room, the reflection order, the gains of the orders and the render threshold as they are now. Safe to call from any thread. */
    vimic::RoomSettings getRoomSettings() const noexcept;

    /** @return The absorption of the walls and the crossovers as they are now. Safe to call from any thread. */
    vimic::Walls getWalls() const noexcept;

    /** @return The cut-off of the air absorption in Hz as it is now. Safe to call from any thread. */
    double getAirCutoff() const noexcept;

    /** @return The delay mode as it is now. Safe to call from any thread. */
    vimic::params::DelayMode getDelayMode() const noexcept;


    /** @return Source `index` (zero-based) as it is now: position, yaw and radiation pattern. Safe to call from any thread. */
    vimic::Source getSource (int index) const noexcept;

    /** @return The microphones for the speakers in `speakers` (their positions, with the yaw and pattern of each). Safe to call from any thread. */
    std::vector<vimic::Microphone> getMicrophones (const tlos::IncludedSpeakers& speakers) const;

    /** @return How many floats of memory the reflection bank has, as a measure for the tests (0 if there is none yet). Message thread. */
    std::size_t getReflectionBankSize() const noexcept { return bankInfo.floats; }

    /** The most images a source can have (the direct sound and 24 reflections). */
    static constexpr int kMaxImages = 25;

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

    /** The memory of the reflections: for every source its splitting into the three absorption bands, and for every source and wall sequence a delay line holding the
        filtered copy of the source (the three bands with the gains of the walls). Made on the message thread (it allocates) and then used by the audio thread alone,
        which is why its parts are mutable. */
    struct ReflectionBank
    {
        int sources = 0;                          ///< How many sources (input channels) it has room for.
        int classes = 0;                          ///< How many wall sequences (filter classes, without the direct sound) it has room for.
        int capacity = 0;                         ///< The longest delay in samples that its lines hold.
        mutable std::vector<vimic::DelayLine> lines;       ///< The delay lines: source s, class c (1 .. classes) is at s * classes + c - 1.
        mutable std::vector<vimic::BandSplitter> splitters;   ///< The band splitter of every source.
        mutable std::array<vimic::BandGains, 24> classGains {};     ///< The band gains of every wall sequence (class c at c - 1) the lines were last written with.
        mutable std::array<float, vimic::params::GlobalCount> designedFor {};   ///< The filter settings that the splitters and the gains were last designed for.
        mutable bool designed = false;                     ///< Whether they have been designed.
    };

    /** What the message thread knows about the bank it published last. */
    struct BankInfo
    {
        bool valid = false;           ///< Whether a bank has been published.
        int sources = 0;              ///< Its number of sources.
        int classes = 0;              ///< Its number of classes.
        int capacity = 0;             ///< Its longest delay.
        int block = 0;                ///< The block size it was made for.
        double sampleRate = 0.0;      ///< The sample rate it was made for.
        std::size_t floats = 0;       ///< Its size in floats.
    };

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

    /** What a source's cached paths were computed from. */
    struct PathKey
    {
        float x = 0.0f;            ///< Source x.
        float y = 0.0f;            ///< Source y.
        float yaw = 0.0f;          ///< Source yaw in degrees.
        float pattern = 1.0f;      ///< Source pattern.
        bool valid = false;        ///< False until computed.
    };

    /** @return The position of path (source, output channel, image) in the flat arrays. */
    static std::size_t at (int source, int channel, int image) noexcept
    {
        return ((std::size_t) source * (std::size_t) tlos::kMaxChannels + (std::size_t) channel) * (std::size_t) kMaxImages + (std::size_t) image;
    }

    /** Message thread. Makes a new reflection bank if the reflections are on and the one that exists does not fit. */
    void refreshBank();

    // Message thread -> audio thread
    tlos::Exchange<MicSet> exchange;           ///< Hands microphone sets to the audio thread.
    tlos::Exchange<ReflectionBank> bankExchange;   ///< Hands reflection banks to the audio thread.
    tlos::IncludedSpeakers publishedMics;      ///< Microphones of the last published set (message thread only).
    bool havePublished = false;                ///< Whether anything has been published (message thread only).
    std::uint64_t nextSetId = 1;               ///< Id of the next published set (message thread only).
    BankInfo bankInfo;                         ///< The last published bank (message thread only).
    std::atomic<bool> bankStale { true };      ///< Set by prepareToPlay(): the bank must be made again.
    std::atomic<double> preparedSampleRate { 0.0 };   ///< The sample rate of the last prepareToPlay().
    std::atomic<int> preparedBlock { 0 };      ///< The block size of the last prepareToPlay().

    std::array<std::atomic<float>*, vimic::params::GlobalCount> globalParam {};   ///< Raw values of the model parameters.
    std::array<std::atomic<float>*, tlos::kMaxChannels> srcYawParam {};   ///< Raw values of the source yaws.
    std::array<std::atomic<float>*, tlos::kMaxChannels> srcPatternParam {};   ///< Raw values of the source patterns.
    std::array<std::atomic<float>*, tlos::kMaxChannels> yawParam {};      ///< Raw values of the microphone yaws.
    std::array<std::atomic<float>*, tlos::kMaxChannels> patternParam {};  ///< Raw values of the microphone patterns.

    // Audio thread only
    double sampleRate = 48000.0;               ///< The sample rate from prepareToPlay().
    std::vector<vimic::DelayLine> lines;       ///< One delay line per input channel: the direct sound.
    std::vector<float> prevGains;              ///< Gains at the end of the previous block (with trim, mute and solo), per path (see at()).
    std::vector<float> panGains;               ///< The gains of the paths alone: what the cache holds.
    std::vector<float> targetGains;            ///< Gains wanted at the end of this block (with trim, mute, solo and the gains of the direct sound and the output).
    std::vector<float> prevDelays;             ///< Delays in samples at the end of the previous block.
    std::vector<float> delays;                 ///< Delays in samples of the paths alone: what the cache holds.
    std::vector<float> targetDelays;           ///< Delays wanted at the end of this block.
    std::vector<float> heldDelay;              ///< Cross-fade mode: the delay a path is rendered with (it only changes by a fade).
    std::vector<float> fadeFrom;               ///< Cross-fade mode: the delay a path is fading away from, while it fades.
    std::vector<int> fadeAt;                   ///< Cross-fade mode: how many samples of its fade a path has had (-1: not fading).
    std::vector<float> bandLow;                ///< A block of the low band of a source.
    std::vector<float> bandMid;                ///< A block of the middle band of a source.
    std::vector<float> bandHigh;               ///< A block of the high band of a source.
    int prevUsedSpeakers = 0;                  ///< Microphones in use in the previous block.
    int lastImageCount = 1;                    ///< How many images (paths per source and microphone) the previous block had, so that paths that fade out are still visited.
    Settings usedSettings;                     ///< What the cached paths were computed with.
    std::array<PathKey, tlos::kMaxChannels> pathKeys {};   ///< Per source: what its paths were computed from.
    std::atomic<bool> restartRamp { true };    ///< Set from any thread: the next block starts at its target gains and delays.
    juce::AudioBuffer<float> inputCopy;        ///< Copy of the inputs, so that outputs may overwrite the buffer.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViMiCAudioProcessor)
};
