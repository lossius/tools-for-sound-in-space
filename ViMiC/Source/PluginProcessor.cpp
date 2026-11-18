// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginProcessor.cpp
    @brief Microphone publishing and the real-time rendering loop of ViMiC. */

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "tlos/Parameters.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace
{
/** Degrees to radians. */
constexpr double kDegreesToRadians = std::numbers::pi / 180.0;

/** The IDs of the model parameters, in the order of ViMiCAudioProcessor::Settings::globals. */
const juce::String* globalIds()
{
    static const juce::String ids[] { vimic::params::kDistanceLawId, vimic::params::kDistancePowerId, vimic::params::kDistanceDbId,
                                      vimic::params::kMinDistanceId, vimic::params::kMicPowerId, vimic::params::kMicPolarityId,
                                      vimic::params::kDelayModeId, vimic::params::kTemperatureId, vimic::params::kSourceHeightId,
                                      vimic::params::kMicHeightId, vimic::params::kDirectGainId, vimic::params::kOutputGainId };
    return ids;
}
}

ViMiCAudioProcessor::ViMiCAudioProcessor()
    : SpatialProcessor (BusesProperties()
                            .withInput  ("Sources",  juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Speakers", juce::AudioChannelSet::stereo(), true),
                        "ViMiC", vimic::params::createParameters())
{
    for (size_t i = 0; i < globalParam.size(); ++i)
        globalParam[i] = apvts.getRawParameterValue (globalIds()[i]);

    for (int i = 0; i < tlos::kMaxChannels; ++i)
    {
        yawParam[(size_t) i] = apvts.getRawParameterValue (vimic::params::micYawId (i));
        patternParam[(size_t) i] = apvts.getRawParameterValue (vimic::params::micPatternId (i));
    }

    setLatencySamples ((int) vimic::DelayLine::kMinDelay);
}

std::vector<tlos::OscParameter> ViMiCAudioProcessor::getOscParameters() const
{
    const char* addresses[] { "/vimic/law", "/vimic/power", "/vimic/db", "/vimic/mindist", "/vimic/micpower", "/vimic/polarity",
                              "/vimic/delaymode", "/vimic/temperature", "/vimic/sourceheight", "/vimic/micheight", "/vimic/directgain", "/vimic/gain" };
    std::vector<tlos::OscParameter> out;
    for (size_t i = 0; i < globalParam.size(); ++i)
        out.push_back ({ addresses[i], globalIds()[i] });
    return out;
}

std::vector<tlos::SpeakerColumn> ViMiCAudioProcessor::getSpeakerColumns() const
{
    const auto yawTitle = "Yaw (" + juce::String::charToString (0xb0) + ")";
    return { tlos::SpeakerColumn { yawTitle, "yaw", [] (int i) { return vimic::params::micYawId (i); }, -180.0, 180.0, 0.0, 1 },
             tlos::SpeakerColumn { "Pattern", "pattern", [] (int i) { return vimic::params::micPatternId (i); }, 0.0, 1.0, 0.5, 2 } };
}

vimic::PathSettings ViMiCAudioProcessor::getPathSettings() const noexcept
{
    const auto v = [this] (size_t i) { return (double) globalParam[i]->load(); };

    vimic::PathSettings s;
    s.law = v (0) >= 0.5 ? vimic::DistanceLaw::Exponential : vimic::DistanceLaw::Inverse;
    s.power = v (1);
    s.dbPerMetre = v (2);
    s.minDistance = v (3);
    s.micPower = (int) std::lround (v (4));
    s.restrictPolarity = v (5) >= 0.5;
    s.temperatureCelsius = v (7);
    s.sourceHeight = v (8);
    s.micHeight = v (9);
    return s;
}

vimic::params::DelayMode ViMiCAudioProcessor::getDelayMode() const noexcept
{
    return globalParam[6]->load() >= 0.5f ? vimic::params::DelayMode::Absolute : vimic::params::DelayMode::Relative;
}

std::vector<vimic::Microphone> ViMiCAudioProcessor::getMicrophones (const tlos::IncludedSpeakers& speakers) const
{
    std::vector<vimic::Microphone> out;
    out.reserve (speakers.positions.size());

    for (size_t i = 0; i < speakers.positions.size(); ++i)
    {
        const int channel = speakers.channelOf[i];
        out.push_back ({ speakers.positions[i], (double) yawParam[(size_t) channel]->load() * kDegreesToRadians,
                         (double) patternParam[(size_t) channel]->load() });
    }

    return out;
}

void ViMiCAudioProcessor::layoutWasReset (bool, bool speakersReset)
{
    if (! speakersReset)
        return;

    // Microphones point outwards: the yaw is the azimuth of the position (counter-clockwise from the front).
    for (int i = 0; i < getActiveSpeakers(); ++i)
    {
        const double x = (double) spkX[(size_t) i]->load(), y = (double) spkY[(size_t) i]->load();
        if (std::hypot (x, y) > 1e-9)
            setParameterValue (vimic::params::micYawId (i), (float) (std::atan2 (-x, y) / kDegreesToRadians));
    }
}

void ViMiCAudioProcessor::refreshLayout()
{
    const auto mics = getIncludedSpeakers();

    const auto same = [] (const tlos::Vec2& a, const tlos::Vec2& b) { return juce::exactlyEqual (a.x, b.x) && juce::exactlyEqual (a.y, b.y); };

    if (havePublished && mics.channelOf == publishedMics.channelOf
        && mics.positions.size() == publishedMics.positions.size()
        && std::equal (mics.positions.begin(), mics.positions.end(), publishedMics.positions.begin(), same))
        return;

    auto set = std::make_unique<MicSet>();
    set->positions = mics.positions;
    set->channelOf = mics.channelOf;
    set->id = nextSetId++;
    exchange.publish (std::move (set));

    publishedMics = mics;
    havePublished = true;
}

void ViMiCAudioProcessor::stateRestored()
{
    restartRamp = true; // start from the restored positions without a ramp
    refreshLayout();
}

void ViMiCAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    const int n = std::min (getTotalNumInputChannels(), tlos::kMaxChannels);
    const int block = std::max (samplesPerBlock, 1);
    inputCopy.setSize (std::max (n, 1), block, false, false, true);

    // One second of every source: the longest path at +-100 m is about 0.8 s.
    lines.assign ((size_t) std::max (n, 1), vimic::DelayLine());
    for (auto& line : lines)
        line.prepare ((int) std::ceil (sampleRate) + 8, block);

    usedSettings.valid = false;
    for (auto& key : pathKeys)
        key.valid = false;
    restartRamp = true;

    // First run only: give the active channels a sensible default layout.
    initialiseLayoutIfFirstRun();
}

void ViMiCAudioProcessor::releaseResources()
{
    exchange.release();
}

void ViMiCAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = std::min (getTotalNumInputChannels(),  tlos::kMaxChannels);
    const int m = std::min (getTotalNumOutputChannels(), tlos::kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    const int chunkMax = inputCopy.getNumSamples();

    if (numSamples == 0)
        return;

    if (inputCopy.getNumChannels() < n || (int) lines.size() < n || buffer.getNumChannels() < std::max (n, m))
    {
        buffer.clear();
        return;
    }

    recordInputLevels (buffer, n);

    const MicSet* mics = exchange.acquire();
    const int usedSources = getActiveSources();
    const int usedSpeakers = getActiveSpeakers();
    const int activeSources = std::min (n, usedSources);

    // The settings and the microphones' orientation, read every block: a change recomputes the paths of every source.
    const auto pathSettings = getPathSettings();
    const bool absolute = getDelayMode() == vimic::params::DelayMode::Absolute;

    std::array<vimic::Microphone, tlos::kMaxChannels> micArray {};
    const int micCount = mics != nullptr ? std::min ((int) mics->channelOf.size(), tlos::kMaxChannels) : 0;

    Settings settings;
    settings.valid = true;
    settings.sampleRate = sampleRate;
    settings.setId = mics != nullptr ? mics->id : 0;
    for (size_t i = 0; i < globalParam.size(); ++i)
        settings.globals[i] = globalParam[i]->load();
    for (int k = 0; k < micCount; ++k)
    {
        const int channel = mics->channelOf[(size_t) k];
        settings.yaw[(size_t) k] = yawParam[(size_t) channel]->load();
        settings.pattern[(size_t) k] = patternParam[(size_t) channel]->load();
        micArray[(size_t) k] = { mics->positions[(size_t) k], (double) settings.yaw[(size_t) k] * kDegreesToRadians, (double) settings.pattern[(size_t) k] };
    }

    const auto sameSettings = [&] (const Settings& a, const Settings& b)
    {
        const auto equal = [] (float p, float q) { return juce::exactlyEqual (p, q); };
        return a.valid && b.valid && a.setId == b.setId && juce::exactlyEqual (a.sampleRate, b.sampleRate)
            && std::equal (a.globals.begin(), a.globals.end(), b.globals.begin(), equal)
            && std::equal (a.yaw.begin(), a.yaw.begin() + micCount, b.yaw.begin(), equal)
            && std::equal (a.pattern.begin(), a.pattern.begin() + micCount, b.pattern.begin(), equal);
    };

    if (! sameSettings (usedSettings, settings))
    {
        const bool modeChanged = usedSettings.valid && ! juce::exactlyEqual (usedSettings.globals[6], settings.globals[6]);
        usedSettings = settings;
        for (auto& key : pathKeys)
            key.valid = false;
        if (modeChanged)
            restartRamp = true;   // a different delay mode is a different set of delays: no sweep from one to the other
    }

    // The time a delay of 0 is: the smallest delay the interpolation allows (the plugin's latency).
    const double minimum = vimic::DelayLine::kMinDelay;
    const double longest = std::ceil (sampleRate);

    for (int s = 0; s < n; ++s)
    {
        auto& key = pathKeys[(size_t) s];
        auto& gainRow = panGains[(size_t) s];
        auto& delayRow = delays[(size_t) s];

        if (s >= activeSources || mics == nullptr || micCount == 0)
        {
            key.valid = false;
            gainRow.fill (0.0f);
            delayRow.fill (0.0f);
            continue;
        }

        // A source that has not moved, with unchanged microphones and settings, keeps its paths.
        const float x = srcX[(size_t) s]->load(), y = srcY[(size_t) s]->load();
        if (key.valid && juce::exactlyEqual (key.x, x) && juce::exactlyEqual (key.y, y))
            continue;

        std::array<vimic::PathResult, tlos::kMaxChannels> paths {};
        double shortest = std::numeric_limits<double>::max();
        for (int k = 0; k < micCount; ++k)
        {
            paths[(size_t) k] = vimic::computeDirectPath ({ (double) x, (double) y }, micArray[(size_t) k], pathSettings);
            shortest = std::min (shortest, paths[(size_t) k].delaySeconds);
        }

        // Relative delays: only the differences between the microphones; absolute: the true time of flight.
        gainRow.fill (0.0f);
        delayRow.fill (0.0f);
        for (int k = 0; k < micCount; ++k)
        {
            const int channel = mics->channelOf[(size_t) k];
            const double seconds = absolute ? paths[(size_t) k].delaySeconds : paths[(size_t) k].delaySeconds - shortest;
            gainRow[(size_t) channel] = (float) paths[(size_t) k].gain;
            delayRow[(size_t) channel] = (float) std::clamp (seconds * sampleRate + minimum, minimum, longest);
        }

        key = { x, y, true };
    }

    // Trim, mute and solo of the sources and microphones, the gain of the direct sound and the output gain scale the path gains.
    tlos::ChannelGains channelGains;
    getChannelGains (channelGains);
    const float levelGain = juce::Decibels::decibelsToGain (globalParam[10]->load(), (float) vimic::params::kMinGainDb)
                          * juce::Decibels::decibelsToGain (globalParam[11]->load(), (float) vimic::params::kMinGainDb);

    for (int s = 0; s < n; ++s)
        for (int k = 0; k < tlos::kMaxChannels; ++k)
            targetGains[(size_t) s][(size_t) k] = panGains[(size_t) s][(size_t) k] * channelGains.sources[(size_t) s] * channelGains.speakers[(size_t) k] * levelGain;

    targetDelays = delays;

    if (restartRamp.exchange (false))
    {
        prevGains = targetGains;
        prevDelays = targetDelays;
        prevUsedSpeakers = usedSpeakers;
    }

    // A path that was silent starts at its new delay (nothing is heard to sweep), and one that becomes silent keeps its last delay.
    for (int s = 0; s < n; ++s)
        for (int k = 0; k < m; ++k)
        {
            auto& previous = prevDelays[(size_t) s][(size_t) k];
            auto& next = targetDelays[(size_t) s][(size_t) k];
            if (juce::exactlyEqual (prevGains[(size_t) s][(size_t) k], 0.0f))
                previous = next;
            else if (juce::exactlyEqual (targetGains[(size_t) s][(size_t) k], 0.0f))
                next = previous;
        }

    // Gains and delays ramp linearly from the previous block's values to the new ones across the whole block.
    // Blocks larger than the prepared size are processed in chunks, keeping the ramps continuous.
    for (int start = 0; start < numSamples; start += chunkMax)
    {
        const int len = std::min (chunkMax, numSamples - start);

        for (int ch = 0; ch < n; ++ch)
            inputCopy.copyFrom (ch, 0, buffer, ch, start, len);

        for (int ch = 0; ch < m; ++ch)
            buffer.clear (ch, start, len);

        const float f0 = (float) start / (float) numSamples;
        const float f1 = (float) (start + len) / (float) numSamples;
        const int speakerLimit = std::max (std::min (m, tlos::kMaxChannels), 0);

        for (int s = 0; s < n; ++s)
        {
            auto& line = lines[(size_t) s];
            line.write (inputCopy.getReadPointer (s), len);
            const auto& prevG = prevGains[(size_t) s];
            const auto& nextG = targetGains[(size_t) s];
            const auto& prevD = prevDelays[(size_t) s];
            const auto& nextD = targetDelays[(size_t) s];

            // The previous gains may belong to a larger set of microphones, so go through every output.
            for (int k = 0; k < speakerLimit; ++k)
            {
                const float g0 = prevG[(size_t) k], g1 = nextG[(size_t) k];
                if (juce::exactlyEqual (g0, 0.0f) && juce::exactlyEqual (g1, 0.0f))
                    continue;

                const float d0 = prevD[(size_t) k], d1 = nextD[(size_t) k];
                line.addTap (buffer.getWritePointer (k) + start, len,
                             (double) (d0 + (d1 - d0) * f0), (double) (d0 + (d1 - d0) * f1),
                             g0 + (g1 - g0) * f0, g0 + (g1 - g0) * f1);
            }
        }
    }

    // Unused microphones are always silent, even while an older set that still contains them is in use (the new one is published within ~33 ms).
    // Outputs that were in use during the previous block fade out over this block; all others are cleared.
    for (int ch = usedSpeakers; ch < m; ++ch)
    {
        if (ch < prevUsedSpeakers)
        {
            float* data = buffer.getWritePointer (ch);
            const float step = 1.0f / (float) numSamples;
            float g = 1.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                data[i] *= g;
                g -= step;
            }
        }
        else
        {
            buffer.clear (ch, 0, numSamples);
        }
    }
    prevUsedSpeakers = usedSpeakers;

    for (int s = 0; s < n; ++s)
    {
        prevGains[(size_t) s] = targetGains[(size_t) s];
        prevDelays[(size_t) s] = targetDelays[(size_t) s];
    }

    recordOutputLevels (buffer, m);
}

juce::AudioProcessorEditor* ViMiCAudioProcessor::createEditor()
{
    return new ViMiCAudioProcessorEditor (*this);
}

/** Entry point used by the JUCE plugin wrappers (VST3, AU, Standalone) to create the processor. */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ViMiCAudioProcessor();
}
