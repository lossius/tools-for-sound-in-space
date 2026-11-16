// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file SpatialProcessor.cpp
    @brief Parameter access, counts, edit modes, groups, virtual speaker setting and state of every plugin. */

#include "tlos/SpatialProcessor.h"
#include "tlos/Parameters.h"

#include <algorithm>
#include <cmath>

namespace tlos
{
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
/// State property: group id per source as text, "0 0 1 1 ..." (see SourceGroups).
const juce::Identifier kSourceGroups { "sourceGroups" };

/** k-th of `count` positions on a ring; k = 0 at the front (+y), going clockwise. */
juce::Point<float> ringPosition (int k, int count, float radius)
{
    const float theta = count == 2 ? (k == 0 ? -juce::MathConstants<float>::pi / 4.0f : juce::MathConstants<float>::pi / 4.0f)
                                   : juce::MathConstants<float>::twoPi * (float) k / (float) std::max (count, 1);
    return { radius * std::sin (theta), radius * std::cos (theta) };
}

/** Number of channels in use: the parameter value rounded and limited to 1..min(channels, 64), or 0 if there are no channels. */
int clampCount (float parameterValue, int channels) noexcept
{
    const int available = std::min (channels, kMaxChannels);
    if (available <= 0)
        return 0;
    return std::clamp ((int) std::lround (parameterValue), 1, available);
}
}

namespace
{
/** The shared parameters followed by the plugin's own. */
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout (std::vector<std::unique_ptr<juce::RangedAudioParameter>> extra)
{
    auto layout = params::createLayout();
    for (auto& parameter : extra)
        layout.add (std::move (parameter));
    return layout;
}
}

SpatialProcessor::SpatialProcessor (const BusesProperties& buses, const juce::String& stateType,
                                    std::vector<std::unique_ptr<juce::RangedAudioParameter>> extraParameters)
    : AudioProcessor (buses),
      apvts (*this, nullptr, stateType, makeLayout (std::move (extraParameters)))
{
    for (int i = 0; i < kMaxChannels; ++i)
    {
        srcX[(size_t) i] = apvts.getRawParameterValue (params::sourceId (i, 'x'));
        srcY[(size_t) i] = apvts.getRawParameterValue (params::sourceId (i, 'y'));
        srcSpread[(size_t) i] = apvts.getRawParameterValue (params::spreadId (i));
        spkX[(size_t) i] = apvts.getRawParameterValue (params::speakerId (i, 'x'));
        spkY[(size_t) i] = apvts.getRawParameterValue (params::speakerId (i, 'y'));
    }

    numSourcesParam  = apvts.getRawParameterValue (params::kNumSourcesId);
    numSpeakersParam = apvts.getRawParameterValue (params::kNumSpeakersId);

    startTimerHz (30);
}

SpatialProcessor::~SpatialProcessor()
{
    stopTimer();
}

bool SpatialProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Sources (n) and speakers (m) may differ. Any enabled layout up to 64 channels is accepted.
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    return isValidChannelCount (in.size()) && isValidChannelCount (out.size());
}

int SpatialProcessor::getActiveSources() const noexcept
{
    return clampCount (numSourcesParam->load(), getTotalNumInputChannels());
}

int SpatialProcessor::getActiveSpeakers() const noexcept
{
    return clampCount (numSpeakersParam->load(), getTotalNumOutputChannels());
}

std::vector<Vec2> SpatialProcessor::getActiveSpeakerPositions() const
{
    const int m = getActiveSpeakers();

    std::vector<Vec2> speakers;
    speakers.reserve ((size_t) m);
    for (int i = 0; i < m; ++i)
        speakers.push_back ({ (double) spkX[(size_t) i]->load(), (double) spkY[(size_t) i]->load() });

    return speakers;
}

void SpatialProcessor::setParameterValue (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

void SpatialProcessor::resetLayoutToRings (bool resetSources, bool resetSpeakers)
{
    const int n = getActiveSources();
    const int m = getActiveSpeakers();

    if (resetSources)
        for (int i = 0; i < n; ++i)
        {
            const auto p = ringPosition (i, n, 2.5f);
            setParameterValue (params::sourceId (i, 'x'), p.x);
            setParameterValue (params::sourceId (i, 'y'), p.y);
        }

    if (resetSpeakers)
        for (int i = 0; i < m; ++i)
        {
            const auto p = ringPosition (i, m, 5.0f);
            setParameterValue (params::speakerId (i, 'x'), p.x);
            setParameterValue (params::speakerId (i, 'y'), p.y);
        }

    apvts.state.setProperty (kLayoutInitialised, true, nullptr);
    refreshLayout();
}

void SpatialProcessor::initialiseLayoutIfFirstRun()
{
    if (! apvts.state.getProperty (kLayoutInitialised, false))
        resetLayoutToRings();
    else
        refreshLayout();
}

bool SpatialProcessor::canEditSources() const
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

bool SpatialProcessor::canEditSpeakers() const
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

void SpatialProcessor::setSourcesEditable (bool editable)
{
    const bool speakers = canEditSpeakers(); // read before the legacy property is dropped
    apvts.state.setProperty (kEditSources, editable, nullptr);
    apvts.state.setProperty (kEditSpeakers, speakers, nullptr);
    apvts.state.removeProperty (kLegacyEditMode, nullptr);
}

void SpatialProcessor::setSpeakersEditable (bool editable)
{
    const bool sources = canEditSources();
    apvts.state.setProperty (kEditSources, sources, nullptr);
    apvts.state.setProperty (kEditSpeakers, editable, nullptr);
    apvts.state.removeProperty (kLegacyEditMode, nullptr);
}

int SpatialProcessor::getEditorWidth() const
{
    return (int) apvts.state.getProperty (kEditorWidth, 0);
}

void SpatialProcessor::setEditorWidth (int width)
{
    apvts.state.setProperty (kEditorWidth, width, nullptr);
}

VirtualMode SpatialProcessor::getVirtualMode() const
{
    if (! supportsVirtualSpeakers())
        return VirtualMode::Off;

    const int value = (int) apvts.state.getProperty (kVirtualMode, (int) VirtualMode::Auto);
    return value >= (int) VirtualMode::Auto && value <= (int) VirtualMode::Off ? (VirtualMode) value : VirtualMode::Auto;
}

void SpatialProcessor::setVirtualMode (VirtualMode mode)
{
    if (mode == VirtualMode::Manual && getVirtualMode() != VirtualMode::Manual && getManualVirtualSpeakers().empty())
    {
        // Start from what is in effect now: the automatic speakers that are at least half in effect.
        std::vector<Vec2> start;
        for (const auto& v : getVirtualSpeakers())
            if (v.strength >= 0.5)
                start.push_back (v.position);
        setManualVirtualSpeakers (start);
    }

    apvts.state.setProperty (kVirtualMode, (int) mode, nullptr);
    refreshLayout();
}

bool SpatialProcessor::canEditVirtualSpeakers() const
{
    return getVirtualMode() == VirtualMode::Manual && canEditSpeakers();
}

std::vector<Vec2> SpatialProcessor::getManualVirtualSpeakers() const
{
    std::vector<Vec2> out;
    const auto tokens = juce::StringArray::fromTokens (apvts.state.getProperty (kManualVirtual).toString(), " ", "");

    for (int i = 0; i + 1 < tokens.size() && (int) out.size() < kMaxVirtualSpeakers; i += 2)
    {
        const double x = tokens[i].getDoubleValue(), y = tokens[i + 1].getDoubleValue();
        if (std::isfinite (x) && std::isfinite (y))
            out.push_back ({ x, y });
    }

    return out;
}

void SpatialProcessor::storeManualVirtualSpeakers (const std::vector<Vec2>& list)
{
    juce::StringArray tokens;
    const double limit = params::kCoordRange;

    for (size_t i = 0; i < list.size() && (int) i < kMaxVirtualSpeakers; ++i)
    {
        tokens.add (juce::String (juce::jlimit (-limit, limit, list[i].x), 4));
        tokens.add (juce::String (juce::jlimit (-limit, limit, list[i].y), 4));
    }

    apvts.state.setProperty (kManualVirtual, tokens.joinIntoString (" "), nullptr);
}

void SpatialProcessor::setManualVirtualSpeakers (const std::vector<Vec2>& list)
{
    storeManualVirtualSpeakers (list);
    refreshLayout();
}

void SpatialProcessor::setVirtualLayout (VirtualMode mode, const std::vector<Vec2>* manualList)
{
    if (manualList != nullptr)
        storeManualVirtualSpeakers (*manualList);

    apvts.state.setProperty (kVirtualMode, (int) mode, nullptr);
    refreshLayout();
}

std::vector<VirtualSpeaker> SpatialProcessor::findAutomaticVirtualSpeakers (const std::vector<Vec2>&) const
{
    return {};
}

std::vector<VirtualSpeaker> SpatialProcessor::getVirtualSpeakers() const
{
    switch (getVirtualMode())
    {
        case VirtualMode::Off:
            return {};

        case VirtualMode::Manual:
        {
            std::vector<VirtualSpeaker> out;
            for (const auto& p : getManualVirtualSpeakers())
                out.push_back ({ p, 1.0 });
            return out;
        }

        case VirtualMode::Auto:
            break;
    }

    return findAutomaticVirtualSpeakers (getActiveSpeakerPositions());
}

SourceGroups SpatialProcessor::getSourceGroups() const
{
    return SourceGroups::fromString (apvts.state.getProperty (kSourceGroups, juce::String()).toString().toStdString());
}

void SpatialProcessor::setSourceGroups (const SourceGroups& groups)
{
    if (groups.any())
        apvts.state.setProperty (kSourceGroups, juce::String (groups.toString()), nullptr);
    else
        apvts.state.removeProperty (kSourceGroups, nullptr);
}

void SpatialProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpatialProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            apvts.state.setProperty (kLayoutInitialised, true, nullptr);
            stateRestored();
        }
}
}
