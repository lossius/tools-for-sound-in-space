// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginProcessor.cpp
    @brief Speaker publishing and the real-time DBAP panning loop. */

#include "PluginProcessor.h"
#include "DistPanParameters.h"
#include "PluginEditor.h"
#include "tlos/Parameters.h"

#include <algorithm>
#include <cmath>

DistPanAudioProcessor::DistPanAudioProcessor()
    : SpatialProcessor (BusesProperties()
                            .withInput  ("Sources",  juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Speakers", juce::AudioChannelSet::stereo(), true),
                        "DistPan", distpan::params::createParameters())
{
    rolloffParam = apvts.getRawParameterValue (distpan::params::kRolloffId);
    blurParam    = apvts.getRawParameterValue (distpan::params::kBlurId);
}

distpan::DbapSettings DistPanAudioProcessor::getSettings() const noexcept
{
    return { (double) rolloffParam->load(), (double) blurParam->load(), getOutsideRolloff() };
}

void DistPanAudioProcessor::refreshLayout()
{
    const auto speakers = getIncludedSpeakers();

    const auto same = [] (const tlos::Vec2& a, const tlos::Vec2& b) { return juce::exactlyEqual (a.x, b.x) && juce::exactlyEqual (a.y, b.y); };

    if (havePublished && speakers.channelOf == publishedSpeakers.channelOf
        && speakers.positions.size() == publishedSpeakers.positions.size()
        && std::equal (speakers.positions.begin(), speakers.positions.end(), publishedSpeakers.positions.begin(), same))
        return;

    auto set = std::make_unique<SpeakerSet>();
    set->positions = speakers.positions;
    set->channelOf = speakers.channelOf;
    set->hull = tlos::Hull (speakers.positions);
    set->id = nextSetId++;
    exchange.publish (std::move (set));

    publishedSpeakers = speakers;
    havePublished = true;
}

void DistPanAudioProcessor::stateRestored()
{
    restartRamp = true; // start from the restored positions without a ramp
    refreshLayout();
}

void DistPanAudioProcessor::prepareToPlay (double, int samplesPerBlock)
{
    const int n = std::min (getTotalNumInputChannels(), tlos::kMaxChannels);
    inputCopy.setSize (std::max (n, 1), std::max (samplesPerBlock, 1), false, false, true);
    restartRamp = true;

    // First run only: give the active channels a sensible default layout.
    initialiseLayoutIfFirstRun();
}

void DistPanAudioProcessor::releaseResources()
{
    exchange.release();
}

void DistPanAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    // Target gains from the current source positions, the settings and the latest published speakers.
    const SpeakerSet* speakers = exchange.acquire();
    const float rolloff = rolloffParam->load(), blur = blurParam->load(), outside = outsideRolloffParam->load();

    // Sources beyond the "Number of Sources" setting get no gains, so they fade out and stay silent.
    // (The speaker set only contains the speakers in use, so unused outputs stay silent too.)
    const int usedSources = getActiveSources();
    const int usedSpeakers = getActiveSpeakers();
    const int activeSources = std::min (n, usedSources);

    for (int s = 0; s < n; ++s)
    {
        auto& key = gainKeys[(size_t) s];
        auto& row = panGains[(size_t) s];

        if (s >= activeSources || speakers == nullptr)
        {
            key.valid = false;
            row.fill (0.0f);
            continue;
        }

        // A source that has not moved, with unchanged speakers and settings, keeps its panning gains.
        const float x = srcX[(size_t) s]->load(), y = srcY[(size_t) s]->load(), spread = srcSpread[(size_t) s]->load();
        if (key.valid && key.setId == speakers->id && juce::exactlyEqual (key.x, x) && juce::exactlyEqual (key.y, y)
            && juce::exactlyEqual (key.spread, spread) && juce::exactlyEqual (key.rolloff, rolloff) && juce::exactlyEqual (key.blur, blur)
            && juce::exactlyEqual (key.outside, outside))
            continue;

        const auto gains = distpan::computeGains (speakers->positions, speakers->hull, { (double) x, (double) y }, (double) spread,
                                                  { (double) rolloff, (double) blur, (double) outside });
        row.fill (0.0f);
        for (int i = 0; i < gains.count; ++i)
            row[(size_t) speakers->channelOf[(size_t) gains.speaker[(size_t) i]]] = gains.gain[(size_t) i];

        key = { x, y, spread, rolloff, blur, outside, speakers->id, true };
    }

    // Trim, mute and solo of the sources and speakers scale the panning gains.
    tlos::ChannelGains channelGains;
    getChannelGains (channelGains);

    for (int s = 0; s < n; ++s)
        for (int k = 0; k < tlos::kMaxChannels; ++k)
            targetGains[(size_t) s][(size_t) k] = panGains[(size_t) s][(size_t) k] * channelGains.sources[(size_t) s] * channelGains.speakers[(size_t) k];

    if (restartRamp.exchange (false))
    {
        prevGains = targetGains;
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
        const int speakerLimit = std::max (std::min (m, tlos::kMaxChannels), 0);

        for (int s = 0; s < n; ++s)
        {
            const float* in = inputCopy.getReadPointer (s);
            const auto& prev = prevGains[(size_t) s];
            const auto& next = targetGains[(size_t) s];

            // The previous gains may belong to a larger speaker set, so go through every output.
            for (int k = 0; k < speakerLimit; ++k)
            {
                const float g0 = prev[(size_t) k], g1 = next[(size_t) k];
                if (g0 == 0.0f && g1 == 0.0f)
                    continue;

                float* out = buffer.getWritePointer (k) + start;
                float a = g0 + (g1 - g0) * f0;
                const float inc = ((g0 + (g1 - g0) * f1) - a) / (float) len;

                for (int i = 0; i < len; ++i)
                {
                    out[i] += in[i] * a;
                    a += inc;
                }
            }
        }
    }

    // Unused speakers are always silent, even while an older speaker set that still contains them is in
    // use (the new one is published within ~33 ms). Outputs that were in use during the previous block
    // fade out over this block; all others are cleared.
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
        prevGains[(size_t) s] = targetGains[(size_t) s];
}

juce::AudioProcessorEditor* DistPanAudioProcessor::createEditor()
{
    return new DistPanAudioProcessorEditor (*this);
}

/** Entry point used by the JUCE plugin wrappers (VST3, AU, Standalone) to create the processor. */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DistPanAudioProcessor();
}
