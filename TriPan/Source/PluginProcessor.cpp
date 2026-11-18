// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginProcessor.cpp
    @brief Triangulation publishing and the real-time panning loop. */

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
/** The gain `g` gives to `speaker`, or 0 if it is not in the list. */
float gainOf (const tripan::SourceGains& g, int speaker) noexcept
{
    for (int i = 0; i < g.count; ++i)
        if (g.speaker[(size_t) i] == speaker)
            return g.gain[(size_t) i];
    return 0.0f;
}

/** True if `speaker` is in the list of `g`. */
bool contains (const tripan::SourceGains& g, int speaker) noexcept
{
    for (int i = 0; i < g.count; ++i)
        if (g.speaker[(size_t) i] == speaker)
            return true;
    return false;
}
}

TriPanAudioProcessor::TriPanAudioProcessor()
    : SpatialProcessor (BusesProperties()
                            .withInput  ("Sources",  juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Speakers", juce::AudioChannelSet::stereo(), true),
                        "TriPan", {}, true)
{
}

void TriPanAudioProcessor::refreshLayout()
{
    const auto speakers = getIncludedSpeakers();
    auto virtualSpeakers = getVirtualSpeakers();

    const auto same = [] (const tlos::Vec2& a, const tlos::Vec2& b) { return juce::exactlyEqual (a.x, b.x) && juce::exactlyEqual (a.y, b.y); };
    const auto sameVirtual = [&] (const tlos::VirtualSpeaker& a, const tlos::VirtualSpeaker& b)
    {
        return same (a.position, b.position) && juce::exactlyEqual (a.strength, b.strength);
    };

    if (havePublished
        && speakers.channelOf == publishedSpeakers.channelOf
        && speakers.positions.size() == publishedSpeakers.positions.size()
        && std::equal (speakers.positions.begin(), speakers.positions.end(), publishedSpeakers.positions.begin(), same)
        && virtualSpeakers.size() == publishedVirtual.size() && std::equal (virtualSpeakers.begin(), virtualSpeakers.end(), publishedVirtual.begin(), sameVirtual))
        return;

    exchange.publish (std::make_unique<const LayoutSnapshot> (speakers, virtualSpeakers));
    publishedSpeakers = speakers;
    publishedVirtual = std::move (virtualSpeakers);
    havePublished = true;
}

std::vector<tlos::VirtualSpeaker> TriPanAudioProcessor::findAutomaticVirtualSpeakers (const std::vector<tlos::Vec2>& speakers) const
{
    {
        const std::lock_guard<std::mutex> lock (automaticCacheLock);
        if (automaticCacheValid && automaticCacheInput.size() == speakers.size()
            && std::equal (speakers.begin(), speakers.end(), automaticCacheInput.begin(), [] (const tlos::Vec2& a, const tlos::Vec2& b) { return ! (a.x < b.x || a.x > b.x || a.y < b.y || a.y > b.y); }))
            return automaticCacheResult;
    }

    auto result = tripan::findVirtualSpeakers (speakers);

    const std::lock_guard<std::mutex> lock (automaticCacheLock);
    automaticCacheInput = speakers;
    automaticCacheResult = result;
    automaticCacheValid = true;
    return result;
}

void TriPanAudioProcessor::stateRestored()
{
    restartRamp = true; // start from the restored positions without a ramp
    refreshLayout();
}

void TriPanAudioProcessor::prepareToPlay (double, int samplesPerBlock)
{
    const int n = std::min (getTotalNumInputChannels(), tlos::kMaxChannels);
    inputCopy.setSize (std::max (n, 1), std::max (samplesPerBlock, 1), false, false, true);
    restartRamp = true;

    // First run only: give the active channels a sensible default layout.
    initialiseLayoutIfFirstRun();
}

void TriPanAudioProcessor::releaseResources()
{
    exchange.release();
}

void TriPanAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = std::min (getTotalNumInputChannels(),  tlos::kMaxChannels);
    const int m = std::min (getTotalNumOutputChannels(), tlos::kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    const int chunkMax = inputCopy.getNumSamples();

    if (numSamples == 0)
        return;

    if (inputCopy.getNumChannels() < n || buffer.getNumChannels() < std::max (n, m))
    {
        buffer.clear();
        return;
    }

    recordInputLevels (buffer, n);

    // Target gains from the current source positions and the latest published triangulation.
    const LayoutSnapshot* snapshot = exchange.acquire();
    const tripan::SpeakerLayout* tri = snapshot != nullptr ? &snapshot->layout : nullptr;

    // Sources beyond the "Number of Sources" setting get no gains, so they fade out and stay silent.
    // (The triangulation only contains the speakers in use, so unused outputs stay silent too.)
    const float outside = outsideRolloffParam->load();
    const int usedSources = getActiveSources();
    const int usedSpeakers = getActiveSpeakers();

    std::array<tripan::SourceGains, tlos::kMaxChannels> target {};
    const int activeSources = std::min (n, usedSources);

    for (int s = 0; s < n; ++s)
    {
        auto& key = gainKeys[(size_t) s];

        if (s >= activeSources || tri == nullptr)
        {
            key.valid = false;
            panGains[(size_t) s] = {};
            continue;
        }

        // A source that has not moved, with an unchanged layout, keeps its panning gains.
        const float x = srcX[(size_t) s]->load(), y = srcY[(size_t) s]->load(), spread = srcSpread[(size_t) s]->load();
        if (key.valid && key.layoutId == tri->id() && juce::exactlyEqual (key.x, x) && juce::exactlyEqual (key.y, y)
            && juce::exactlyEqual (key.spread, spread) && juce::exactlyEqual (key.outside, outside))
            continue;

        auto gains = tripan::computeGains (*tri, { (double) x, (double) y }, (double) spread, (double) outside);

        // The triangulation numbers the speakers that take part; the gains go to their output channels.
        for (int i = 0; i < gains.count; ++i)
            gains.speaker[(size_t) i] = snapshot->channelOf[(size_t) gains.speaker[(size_t) i]];

        panGains[(size_t) s] = gains;
        key = { x, y, spread, outside, tri->id(), true };
    }

    // Trim, mute and solo of the sources and speakers scale the panning gains.
    tlos::ChannelGains channelGains;
    getChannelGains (channelGains);

    for (int s = 0; s < activeSources; ++s)
    {
        auto g = panGains[(size_t) s];
        for (int i = 0; i < g.count; ++i)
            g.gain[(size_t) i] *= channelGains.sources[(size_t) s] * channelGains.speakers[(size_t) g.speaker[(size_t) i]];
        target[(size_t) s] = g;
    }

    if (restartRamp.exchange (false))
    {
        prevGains = target;
        prevUsedSpeakers = usedSpeakers;
    }

    // Gains ramp linearly from the previous block's values to the new ones across the whole block.
    // Blocks larger than the prepared size are processed in chunks, keeping the ramp continuous.
    for (int start = 0; start < numSamples; start += chunkMax)
    {
        const int len = std::min (chunkMax, numSamples - start);

        for (int ch = 0; ch < n; ++ch)
            inputCopy.copyFrom (ch, 0, buffer, ch, start, len);

        for (int ch = 0; ch < m; ++ch)
            buffer.clear (ch, start, len);

        const float f0 = (float) start / (float) numSamples;
        const float f1 = (float) (start + len) / (float) numSamples;

        const auto accumulate = [&] (int speaker, float g0, float g1, const float* in)
        {
            if (speaker < 0 || speaker >= m || (g0 == 0.0f && g1 == 0.0f))
                return;

            float* out = buffer.getWritePointer (speaker) + start;
            float a = g0 + (g1 - g0) * f0;
            const float inc = ((g0 + (g1 - g0) * f1) - a) / (float) len;

            // A constant gain uses the SIMD multiply-add; a ramp computes a + inc * i, which has no
            // sample-to-sample dependency, so the compiler can vectorise it.
            if (juce::exactlyEqual (g0, g1))
            {
                juce::FloatVectorOperations::addWithMultiply (out, in, a, len);
            }
            else
            {
                for (int i = 0; i < len; ++i)
                    out[i] += in[i] * (a + inc * (float) i);
            }
        };

        for (int s = 0; s < n; ++s)
        {
            const float* in = inputCopy.getReadPointer (s);
            const auto& prev = prevGains[(size_t) s];
            const auto& next = target[(size_t) s];

            for (int i = 0; i < prev.count; ++i)
                accumulate (prev.speaker[(size_t) i], prev.gain[(size_t) i], gainOf (next, prev.speaker[(size_t) i]), in);

            for (int i = 0; i < next.count; ++i)
                if (! contains (prev, next.speaker[(size_t) i]))
                    accumulate (next.speaker[(size_t) i], 0.0f, next.gain[(size_t) i], in);
        }
    }

    // Unused speakers are always silent, even while an older triangulation that still contains
    // them is in use (the new one is published within ~33 ms). Outputs that were in use during
    // the previous block fade out over this block; all others are cleared.
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
        prevGains[(size_t) s] = target[(size_t) s];
    recordOutputLevels (buffer, m);
}

juce::AudioProcessorEditor* TriPanAudioProcessor::createEditor()
{
    return new TriPanAudioProcessorEditor (*this);
}

/** Entry point used by the JUCE plugin wrappers (VST3, AU, Standalone) to create the processor. */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TriPanAudioProcessor();
}
