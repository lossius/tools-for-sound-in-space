// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginProcessor.cpp
    @brief Parameter access, state, triangulation publishing and the real-time panning loop. */

#include "PluginProcessor.h"
#include "Parameters.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
/// State property: set once the default ring layout has been applied.
const juce::Identifier kLayoutInitialised { "layoutInitialised" };
/// State property: whether source positions can be edited in the editor.
const juce::Identifier kEditSources { "editSources" };
/// State property: whether speaker positions can be edited in the editor.
const juce::Identifier kEditSpeakers { "editSpeakers" };
/// Old single mode property from early versions (1 sources, 2 speakers, 3 both, 4 view only); still read.
const juce::Identifier kLegacyEditMode { "editMode" };
/// State property: width of the editor window in pixels.
const juce::Identifier kEditorWidth { "editorWidth" };
/// State property: the VirtualMode as an integer.
const juce::Identifier kVirtualMode { "virtualMode" };
/// State property: manual virtual speakers as text, "x1 y1 x2 y2 ...".
const juce::Identifier kManualVirtual { "manualVirtual" };

/** k-th of `count` positions on a ring; k = 0 at the front (+y), going clockwise. */
juce::Point<float> ringPosition (int k, int count, float radius)
{
    const float theta = count == 2 ? (k == 0 ? -juce::MathConstants<float>::pi / 4.0f : juce::MathConstants<float>::pi / 4.0f)
                                   : juce::MathConstants<float>::twoPi * (float) k / (float) std::max (count, 1);
    return { radius * std::sin (theta), radius * std::cos (theta) };
}

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
    : AudioProcessor (BusesProperties()
                          .withInput  ("Sources",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Speakers", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TriPan", tripan::params::createLayout())
{
    for (int i = 0; i < tripan::kMaxChannels; ++i)
    {
        srcX[(size_t) i] = apvts.getRawParameterValue (tripan::params::sourceId (i, 'x'));
        srcY[(size_t) i] = apvts.getRawParameterValue (tripan::params::sourceId (i, 'y'));
        srcSpread[(size_t) i] = apvts.getRawParameterValue (tripan::params::spreadId (i));
        spkX[(size_t) i] = apvts.getRawParameterValue (tripan::params::speakerId (i, 'x'));
        spkY[(size_t) i] = apvts.getRawParameterValue (tripan::params::speakerId (i, 'y'));
    }

    numSourcesParam  = apvts.getRawParameterValue (tripan::params::kNumSourcesId);
    numSpeakersParam = apvts.getRawParameterValue (tripan::params::kNumSpeakersId);

    startTimerHz (30);
}

TriPanAudioProcessor::~TriPanAudioProcessor()
{
    stopTimer();
}

namespace
{
/** Number of channels in use: the parameter value rounded and limited to 1..min(channels, 64), or 0 if there are no channels. */
int clampCount (float parameterValue, int channels) noexcept
{
    const int available = std::min (channels, tripan::kMaxChannels);
    if (available <= 0)
        return 0;
    return std::clamp ((int) std::lround (parameterValue), 1, available);
}
}

int TriPanAudioProcessor::getActiveSources() const noexcept
{
    return clampCount (numSourcesParam->load(), getTotalNumInputChannels());
}

int TriPanAudioProcessor::getActiveSpeakers() const noexcept
{
    return clampCount (numSpeakersParam->load(), getTotalNumOutputChannels());
}

void TriPanAudioProcessor::setParameterValue (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

void TriPanAudioProcessor::resetLayoutToRings (bool resetSources, bool resetSpeakers)
{
    const int n = getActiveSources();
    const int m = getActiveSpeakers();

    if (resetSources)
        for (int i = 0; i < n; ++i)
        {
            const auto p = ringPosition (i, n, 2.5f);
            setParameterValue (tripan::params::sourceId (i, 'x'), p.x);
            setParameterValue (tripan::params::sourceId (i, 'y'), p.y);
        }

    if (resetSpeakers)
        for (int i = 0; i < m; ++i)
        {
            const auto p = ringPosition (i, m, 5.0f);
            setParameterValue (tripan::params::speakerId (i, 'x'), p.x);
            setParameterValue (tripan::params::speakerId (i, 'y'), p.y);
        }

    apvts.state.setProperty (kLayoutInitialised, true, nullptr);
    refreshTriangulation();
}

bool TriPanAudioProcessor::canEditSources() const
{
    if (apvts.state.hasProperty (kEditSources))
        return (bool) apvts.state.getProperty (kEditSources);

    if (apvts.state.hasProperty (kLegacyEditMode)) // state saved by an earlier version
    {
        const int mode = (int) apvts.state.getProperty (kLegacyEditMode);
        return mode == 1 || mode == 3;
    }

    return true;
}

bool TriPanAudioProcessor::canEditSpeakers() const
{
    if (apvts.state.hasProperty (kEditSpeakers))
        return (bool) apvts.state.getProperty (kEditSpeakers);

    if (apvts.state.hasProperty (kLegacyEditMode))
    {
        const int mode = (int) apvts.state.getProperty (kLegacyEditMode);
        return mode == 2 || mode == 3;
    }

    return false;
}

void TriPanAudioProcessor::setSourcesEditable (bool editable)
{
    const bool speakers = canEditSpeakers(); // read before the legacy property is dropped
    apvts.state.setProperty (kEditSources, editable, nullptr);
    apvts.state.setProperty (kEditSpeakers, speakers, nullptr);
    apvts.state.removeProperty (kLegacyEditMode, nullptr);
}

void TriPanAudioProcessor::setSpeakersEditable (bool editable)
{
    const bool sources = canEditSources();
    apvts.state.setProperty (kEditSources, sources, nullptr);
    apvts.state.setProperty (kEditSpeakers, editable, nullptr);
    apvts.state.removeProperty (kLegacyEditMode, nullptr);
}

int TriPanAudioProcessor::getEditorWidth() const
{
    return (int) apvts.state.getProperty (kEditorWidth, 0);
}

void TriPanAudioProcessor::setEditorWidth (int width)
{
    apvts.state.setProperty (kEditorWidth, width, nullptr);
}

void TriPanAudioProcessor::refreshTriangulation()
{
    const int m = getActiveSpeakers();

    std::vector<tripan::Vec2> speakers;
    speakers.reserve ((size_t) m);
    for (int i = 0; i < m; ++i)
        speakers.push_back ({ (double) spkX[(size_t) i]->load(), (double) spkY[(size_t) i]->load() });

    auto virtualSpeakers = getVirtualSpeakers();

    const auto same = [] (const tripan::Vec2& a, const tripan::Vec2& b) { return juce::exactlyEqual (a.x, b.x) && juce::exactlyEqual (a.y, b.y); };
    const auto sameVirtual = [&] (const tripan::VirtualSpeaker& a, const tripan::VirtualSpeaker& b)
    {
        return same (a.position, b.position) && juce::exactlyEqual (a.strength, b.strength);
    };

    if (havePublished
        && speakers.size() == publishedSpeakers.size() && std::equal (speakers.begin(), speakers.end(), publishedSpeakers.begin(), same)
        && virtualSpeakers.size() == publishedVirtual.size() && std::equal (virtualSpeakers.begin(), virtualSpeakers.end(), publishedVirtual.begin(), sameVirtual))
        return;

    exchange.publish (std::make_unique<const tripan::SpeakerLayout> (speakers, virtualSpeakers));
    publishedSpeakers = std::move (speakers);
    publishedVirtual = std::move (virtualSpeakers);
    havePublished = true;
}

VirtualMode TriPanAudioProcessor::getVirtualMode() const
{
    const int value = (int) apvts.state.getProperty (kVirtualMode, (int) VirtualMode::Auto);
    return value >= (int) VirtualMode::Auto && value <= (int) VirtualMode::Off ? (VirtualMode) value : VirtualMode::Auto;
}

void TriPanAudioProcessor::setVirtualMode (VirtualMode mode)
{
    if (mode == VirtualMode::Manual && getVirtualMode() != VirtualMode::Manual && getManualVirtualSpeakers().empty())
    {
        // Start from what is in effect now: the automatic speakers that are at least half in effect.
        std::vector<tripan::Vec2> start;
        for (const auto& v : getVirtualSpeakers())
            if (v.strength >= 0.5)
                start.push_back (v.position);
        setManualVirtualSpeakers (start);
    }

    apvts.state.setProperty (kVirtualMode, (int) mode, nullptr);
    refreshTriangulation();
}

bool TriPanAudioProcessor::canEditVirtualSpeakers() const
{
    return getVirtualMode() == VirtualMode::Manual && canEditSpeakers();
}

std::vector<tripan::Vec2> TriPanAudioProcessor::getManualVirtualSpeakers() const
{
    std::vector<tripan::Vec2> out;
    const auto tokens = juce::StringArray::fromTokens (apvts.state.getProperty (kManualVirtual).toString(), " ", "");

    for (int i = 0; i + 1 < tokens.size() && (int) out.size() < kMaxVirtualSpeakers; i += 2)
    {
        const double x = tokens[i].getDoubleValue(), y = tokens[i + 1].getDoubleValue();
        if (std::isfinite (x) && std::isfinite (y))
            out.push_back ({ x, y });
    }

    return out;
}

void TriPanAudioProcessor::storeManualVirtualSpeakers (const std::vector<tripan::Vec2>& list)
{
    juce::StringArray tokens;
    const double limit = tripan::params::kCoordRange;

    for (size_t i = 0; i < list.size() && (int) i < kMaxVirtualSpeakers; ++i)
    {
        tokens.add (juce::String (juce::jlimit (-limit, limit, list[i].x), 4));
        tokens.add (juce::String (juce::jlimit (-limit, limit, list[i].y), 4));
    }

    apvts.state.setProperty (kManualVirtual, tokens.joinIntoString (" "), nullptr);
}

void TriPanAudioProcessor::setManualVirtualSpeakers (const std::vector<tripan::Vec2>& list)
{
    storeManualVirtualSpeakers (list);
    refreshTriangulation();
}

void TriPanAudioProcessor::setVirtualLayout (VirtualMode mode, const std::vector<tripan::Vec2>* manualList)
{
    if (manualList != nullptr)
        storeManualVirtualSpeakers (*manualList);

    apvts.state.setProperty (kVirtualMode, (int) mode, nullptr);
    refreshTriangulation();
}

std::vector<tripan::VirtualSpeaker> TriPanAudioProcessor::getVirtualSpeakers() const
{
    switch (getVirtualMode())
    {
        case VirtualMode::Off:
            return {};

        case VirtualMode::Manual:
        {
            std::vector<tripan::VirtualSpeaker> out;
            for (const auto& p : getManualVirtualSpeakers())
                out.push_back ({ p, 1.0 });
            return out;
        }

        case VirtualMode::Auto:
            break;
    }

    const int m = getActiveSpeakers();
    std::vector<tripan::Vec2> speakers;
    speakers.reserve ((size_t) m);
    for (int i = 0; i < m; ++i)
        speakers.push_back ({ (double) spkX[(size_t) i]->load(), (double) spkY[(size_t) i]->load() });

    return tripan::findVirtualSpeakers (speakers);
}

void TriPanAudioProcessor::prepareToPlay (double, int samplesPerBlock)
{
    const int n = std::min (getTotalNumInputChannels(), tripan::kMaxChannels);
    inputCopy.setSize (std::max (n, 1), std::max (samplesPerBlock, 1), false, false, true);
    restartRamp = true;

    // First run only: give the active channels a sensible default layout. A restored session
    // carries the flag in its state and is left untouched.
    if (! apvts.state.getProperty (kLayoutInitialised, false))
        resetLayoutToRings();
    else
        refreshTriangulation();
}

void TriPanAudioProcessor::releaseResources()
{
    exchange.release();
}

bool TriPanAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Sources (n) and speakers (m) may differ. Any enabled layout up to 64 channels is accepted:
    // discrete, standard, or ambisonic (orders 1-7 are used only as channel-count carriers).
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    return tripan::isValidChannelCount (in.size()) && tripan::isValidChannelCount (out.size());
}

void TriPanAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = std::min (getTotalNumInputChannels(),  tripan::kMaxChannels);
    const int m = std::min (getTotalNumOutputChannels(), tripan::kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    const int chunkMax = inputCopy.getNumSamples();

    if (numSamples == 0)
        return;

    if (inputCopy.getNumChannels() < n || buffer.getNumChannels() < std::max (n, m))
    {
        buffer.clear();
        return;
    }

    // Target gains from the current source positions and the latest published triangulation.
    const tripan::SpeakerLayout* tri = exchange.acquire();

    // Sources beyond the "Number of Sources" setting get no gains, so they fade out and stay silent.
    // (The triangulation only contains the speakers in use, so unused outputs stay silent too.)
    const int usedSources = getActiveSources();
    const int usedSpeakers = getActiveSpeakers();

    std::array<tripan::SourceGains, tripan::kMaxChannels> target {};
    const int activeSources = std::min (n, usedSources);

    for (int s = 0; s < n; ++s)
    {
        auto& key = gainKeys[(size_t) s];

        if (s >= activeSources || tri == nullptr)
        {
            key.valid = false;
            continue;
        }

        // A source that has not moved, with an unchanged layout, keeps last block's gains.
        const float x = srcX[(size_t) s]->load(), y = srcY[(size_t) s]->load(), spread = srcSpread[(size_t) s]->load();
        if (key.valid && key.layoutId == tri->id() && juce::exactlyEqual (key.x, x) && juce::exactlyEqual (key.y, y)
            && juce::exactlyEqual (key.spread, spread))
        {
            target[(size_t) s] = prevGains[(size_t) s];
            continue;
        }

        target[(size_t) s] = tripan::computeGains (*tri, { (double) x, (double) y }, (double) spread);
        key = { x, y, spread, tri->id(), true };
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

            for (int i = 0; i < len; ++i)
            {
                out[i] += in[i] * a;
                a += inc;
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
}

void TriPanAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TriPanAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            apvts.state.setProperty (kLayoutInitialised, true, nullptr);
            restartRamp = true; // start from the restored positions without a ramp
            refreshTriangulation();
        }
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
