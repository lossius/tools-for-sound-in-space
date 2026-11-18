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
/// State property: excluded speakers as text of 0 and 1, one character per speaker.
const juce::Identifier kExcludedSpeakers { "excludedSpeakers" };
/// State property: the room half extent in metres.
const juce::Identifier kRoomHalf { "roomHalfExtent" };
/// State property: the CoordinateMode as an integer (0 Cartesian, 1 Polar).
const juce::Identifier kCoordinateMode { "coordinateMode" };
/// State property: whether OSC listens.
const juce::Identifier kOscReceive { "oscReceive" };
/// State property: the port OSC listens on.
const juce::Identifier kOscReceivePort { "oscReceivePort" };
/// State property: whether OSC sends changes.
const juce::Identifier kOscSend { "oscSend" };
/// State property: the host OSC sends to.
const juce::Identifier kOscSendHost { "oscSendHost" };
/// State property: the port OSC sends to.
const juce::Identifier kOscSendPort { "oscSendPort" };
/// State property: the OscFormat as an integer.
const juce::Identifier kOscFormat { "oscFormat" };
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
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout (params::PluginParameters extra, bool virtualSpeakers)
{
    return params::createLayout (virtualSpeakers, std::move (extra));
}
}

SpatialProcessor::SpatialProcessor (const BusesProperties& buses, const juce::String& stateType,
                                    params::PluginParameters extraParameters,
                                    bool virtualSpeakerParameters)
    : AudioProcessor (buses),
      apvts (*this, nullptr, stateType, makeLayout (std::move (extraParameters), virtualSpeakerParameters)),
      hasVirtualParameters (virtualSpeakerParameters)
{
    for (int i = 0; i < kMaxChannels; ++i)
    {
        srcX[(size_t) i] = apvts.getRawParameterValue (params::sourceId (i, 'x'));
        srcY[(size_t) i] = apvts.getRawParameterValue (params::sourceId (i, 'y'));
        srcSpread[(size_t) i] = apvts.getRawParameterValue (params::spreadId (i));
        spkX[(size_t) i] = apvts.getRawParameterValue (params::speakerId (i, 'x'));
        spkY[(size_t) i] = apvts.getRawParameterValue (params::speakerId (i, 'y'));
        srcTrim[(size_t) i] = apvts.getRawParameterValue (params::sourceTrimId (i));
        srcMute[(size_t) i] = apvts.getRawParameterValue (params::sourceMuteId (i));
        srcSolo[(size_t) i] = apvts.getRawParameterValue (params::sourceSoloId (i));
        spkTrim[(size_t) i] = apvts.getRawParameterValue (params::speakerTrimId (i));
        spkMute[(size_t) i] = apvts.getRawParameterValue (params::speakerMuteId (i));
        spkSolo[(size_t) i] = apvts.getRawParameterValue (params::speakerSoloId (i));
        srcGroup[(size_t) i] = apvts.getRawParameterValue (params::sourceGroupId (i));
        spkExclude[(size_t) i] = apvts.getRawParameterValue (params::speakerExcludeId (i));
    }

    if (hasVirtualParameters)
    {
        virtualModeParam = apvts.getRawParameterValue (params::kVirtualModeId);
        virtualCountParam = apvts.getRawParameterValue (params::kNumVirtualId);
        for (int i = 0; i < kMaxVirtualSpeakers; ++i)
        {
            virtualX[(size_t) i] = apvts.getRawParameterValue (params::virtualId (i, 'x'));
            virtualY[(size_t) i] = apvts.getRawParameterValue (params::virtualId (i, 'y'));
        }
    }

    numSourcesParam  = apvts.getRawParameterValue (params::kNumSourcesId);
    numSpeakersParam = apvts.getRawParameterValue (params::kNumSpeakersId);
    outsideRolloffParam = apvts.getRawParameterValue (params::kOutsideRolloffId);

    osc = std::make_unique<OscControl> (*this);
    startTimerHz (30);
}

SpatialProcessor::~SpatialProcessor()
{
    stopTimer();
    osc.reset();
}

OscSettings SpatialProcessor::getOscSettings() const
{
    OscSettings s;
    const auto& state = apvts.state;
    s.receive = (bool) state.getProperty (kOscReceive, s.receive);
    s.receivePort = juce::jlimit (1, 65535, (int) state.getProperty (kOscReceivePort, s.receivePort));
    s.send = (bool) state.getProperty (kOscSend, s.send);
    s.sendHost = state.getProperty (kOscSendHost, s.sendHost).toString();
    s.sendPort = juce::jlimit (1, 65535, (int) state.getProperty (kOscSendPort, s.sendPort));
    const int format = (int) state.getProperty (kOscFormat, (int) s.format);
    s.format = format >= 0 && format <= 2 ? (OscFormat) format : s.format;
    return s;
}

void SpatialProcessor::setOscSettings (const OscSettings& s)
{
    auto& state = apvts.state;
    state.setProperty (kOscReceive, s.receive, nullptr);
    state.setProperty (kOscReceivePort, s.receivePort, nullptr);
    state.setProperty (kOscSend, s.send, nullptr);
    state.setProperty (kOscSendHost, s.sendHost, nullptr);
    state.setProperty (kOscSendPort, s.sendPort, nullptr);
    state.setProperty (kOscFormat, (int) s.format, nullptr);
    osc->configure (getOscSettings());
}

juce::String SpatialProcessor::getOscStatus() const
{
    return osc->getStatus();
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

double SpatialProcessor::getOutsideRolloff() const noexcept
{
    return (double) outsideRolloffParam->load();
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

void SpatialProcessor::writeParameters (const std::vector<std::pair<juce::String, float>>& changes, bool gestures)
{
    std::vector<std::pair<juce::RangedAudioParameter*, float>> todo;

    for (const auto& [id, value] : changes)
        if (auto* parameter = apvts.getParameter (id))
        {
            const float normalised = parameter->convertTo0to1 (value);
            if (std::abs (parameter->getValue() - normalised) > 1e-6f)
                todo.emplace_back (parameter, normalised);
        }

    if (gestures)
        for (auto& change : todo)
            change.first->beginChangeGesture();

    for (auto& change : todo)
        change.first->setValueNotifyingHost (change.second);

    if (gestures)
        for (auto& change : todo)
            change.first->endChangeGesture();
}

void SpatialProcessor::getChannelGains (ChannelGains& gains) const noexcept
{
    const auto fill = [] (std::array<float, kMaxChannels>& out, int used,
                          const std::array<std::atomic<float>*, kMaxChannels>& trim,
                          const std::array<std::atomic<float>*, kMaxChannels>& mute,
                          const std::array<std::atomic<float>*, kMaxChannels>& solo)
    {
        bool anySolo = false;
        for (int i = 0; i < used; ++i)
            anySolo = anySolo || solo[(size_t) i]->load() >= 0.5f;

        for (int i = 0; i < kMaxChannels; ++i)
        {
            const bool audible = i < used && mute[(size_t) i]->load() < 0.5f && (! anySolo || solo[(size_t) i]->load() >= 0.5f);
            out[(size_t) i] = audible ? juce::Decibels::decibelsToGain (trim[(size_t) i]->load()) : 0.0f;
        }
    };

    fill (gains.sources,  getActiveSources(),  srcTrim, srcMute, srcSolo);
    fill (gains.speakers, getActiveSpeakers(), spkTrim, spkMute, spkSolo);
}

std::array<bool, kMaxChannels> SpatialProcessor::getExcludedSpeakers() const
{
    std::array<bool, kMaxChannels> out {};
    for (int i = 0; i < kMaxChannels; ++i)
        out[(size_t) i] = spkExclude[(size_t) i]->load() >= 0.5f;
    return out;
}

void SpatialProcessor::setExcludedSpeakers (const std::array<bool, kMaxChannels>& excluded)
{
    if (excluded == getExcludedSpeakers())
        return;

    std::vector<std::pair<juce::String, float>> changes;
    for (int i = 0; i < kMaxChannels; ++i)
        changes.emplace_back (params::speakerExcludeId (i), excluded[(size_t) i] ? 1.0f : 0.0f);

    writeParameters (changes);
    refreshLayout();
}

bool SpatialProcessor::isSpeakerExcluded (int index) const
{
    return index >= 0 && index < kMaxChannels && spkExclude[(size_t) index]->load() >= 0.5f;
}

void SpatialProcessor::setSpeakerExcluded (int index, bool excluded)
{
    if (index < 0 || index >= kMaxChannels)
        return;

    writeParameters ({ { params::speakerExcludeId (index), excluded ? 1.0f : 0.0f } });
    refreshLayout();
}

IncludedSpeakers SpatialProcessor::getIncludedSpeakers() const
{
    const int m = getActiveSpeakers();
    const auto excluded = getExcludedSpeakers();

    IncludedSpeakers out;
    for (int i = 0; i < m; ++i)
        if (! excluded[(size_t) i])
        {
            out.positions.push_back ({ (double) spkX[(size_t) i]->load(), (double) spkY[(size_t) i]->load() });
            out.channelOf.push_back (i);
        }

    return out;
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
            const auto p = ringPosition (i, n, (float) (0.25 * getRoomHalfExtent()));
            setParameterValue (params::sourceId (i, 'x'), p.x);
            setParameterValue (params::sourceId (i, 'y'), p.y);
        }

    if (resetSpeakers)
        for (int i = 0; i < m; ++i)
        {
            const auto p = ringPosition (i, m, (float) (0.5 * getRoomHalfExtent()));
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

double SpatialProcessor::getRoomHalfExtent() const
{
    const double value = (double) apvts.state.getProperty (kRoomHalf, params::kDefaultRoomHalf);
    return std::isfinite (value) ? juce::jlimit (params::kMinRoomHalf, params::kMaxRoomHalf, value) : params::kDefaultRoomHalf;
}

void SpatialProcessor::setRoomHalfExtent (double metres)
{
    if (! std::isfinite (metres))
        return;

    apvts.state.setProperty (kRoomHalf, juce::jlimit (params::kMinRoomHalf, params::kMaxRoomHalf, metres), nullptr);
}

SpatialProcessor::CoordinateMode SpatialProcessor::getCoordinateMode() const
{
    if (! apvts.state.hasProperty (kCoordinateMode))
        return defaultCoordinateMode();

    return (int) apvts.state.getProperty (kCoordinateMode) == 1 ? CoordinateMode::Polar : CoordinateMode::Cartesian;
}

void SpatialProcessor::setCoordinateMode (CoordinateMode mode)
{
    apvts.state.setProperty (kCoordinateMode, mode == CoordinateMode::Polar ? 1 : 0, nullptr);
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
    if (! supportsVirtualSpeakers() || virtualModeParam == nullptr)
        return VirtualMode::Off;

    const int value = 1 + (int) std::lround (virtualModeParam->load());
    return value >= (int) VirtualMode::Auto && value <= (int) VirtualMode::Off ? (VirtualMode) value : VirtualMode::Auto;
}

void SpatialProcessor::setVirtualMode (VirtualMode mode)
{
    if (! hasVirtualParameters)
        return;

    if (mode == VirtualMode::Manual && getVirtualMode() != VirtualMode::Manual && getManualVirtualSpeakers().empty())
    {
        // Start from what is in effect now: the automatic speakers that are at least half in effect.
        std::vector<Vec2> start;
        for (const auto& v : getVirtualSpeakers())
            if (v.strength >= 0.5)
                start.push_back (v.position);
        setVirtualLayout (mode, &start);
        return;
    }

    writeParameters ({ { params::kVirtualModeId, (float) ((int) mode - 1) } });
    refreshLayout();
}

bool SpatialProcessor::canEditVirtualSpeakers() const
{
    return getVirtualMode() == VirtualMode::Manual && canEditSpeakers();
}

std::vector<Vec2> SpatialProcessor::getManualVirtualSpeakers() const
{
    std::vector<Vec2> out;
    if (! hasVirtualParameters)
        return out;

    const int count = juce::jlimit (0, (int) kMaxVirtualSpeakers, (int) std::lround (virtualCountParam->load()));
    for (int i = 0; i < count; ++i)
        out.push_back ({ (double) virtualX[(size_t) i]->load(), (double) virtualY[(size_t) i]->load() });

    return out;
}

void SpatialProcessor::setManualVirtualSpeakers (const std::vector<Vec2>& list, bool ownGesture)
{
    if (! hasVirtualParameters)
        return;

    const int oldCount = juce::jlimit (0, (int) kMaxVirtualSpeakers, (int) std::lround (virtualCountParam->load()));
    const int count = juce::jmin ((int) list.size(), (int) kMaxVirtualSpeakers);

    std::vector<std::pair<juce::String, float>> changes;
    changes.emplace_back (params::kNumVirtualId, (float) count);
    for (int i = 0; i < count; ++i)
    {
        changes.emplace_back (params::virtualId (i, 'x'), std::isfinite (list[(size_t) i].x) ? (float) list[(size_t) i].x : 0.0f);
        changes.emplace_back (params::virtualId (i, 'y'), std::isfinite (list[(size_t) i].y) ? (float) list[(size_t) i].y : 0.0f);
    }
    for (int i = count; i < oldCount; ++i) // what was deleted goes back to 0, so that nothing stale is left behind
    {
        changes.emplace_back (params::virtualId (i, 'x'), 0.0f);
        changes.emplace_back (params::virtualId (i, 'y'), 0.0f);
    }

    writeParameters (changes, ownGesture);
    refreshLayout();
}

void SpatialProcessor::beginVirtualSpeakerGesture (int index)
{
    if (hasVirtualParameters && index >= 0 && index < (int) kMaxVirtualSpeakers)
    {
        apvts.getParameter (params::virtualId (index, 'x'))->beginChangeGesture();
        apvts.getParameter (params::virtualId (index, 'y'))->beginChangeGesture();
    }
}

void SpatialProcessor::endVirtualSpeakerGesture (int index)
{
    if (hasVirtualParameters && index >= 0 && index < (int) kMaxVirtualSpeakers)
    {
        apvts.getParameter (params::virtualId (index, 'x'))->endChangeGesture();
        apvts.getParameter (params::virtualId (index, 'y'))->endChangeGesture();
    }
}

void SpatialProcessor::setVirtualLayout (VirtualMode mode, const std::vector<Vec2>* manualList)
{
    if (! hasVirtualParameters)
        return;

    if (manualList != nullptr)
        setManualVirtualSpeakers (*manualList, false); // together with the mode below, in one step for the host's undo

    writeParameters ({ { params::kVirtualModeId, (float) ((int) mode - 1) } });
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

    return findAutomaticVirtualSpeakers (getIncludedSpeakers().positions); // excluded speakers do not exist for the panning
}

SourceGroups SpatialProcessor::getSourceGroups() const
{
    SourceGroups::Ids ids {};
    for (int i = 0; i < kMaxChannels; ++i)
        ids[(size_t) i] = (int) std::lround (srcGroup[(size_t) i]->load());

    return SourceGroups (ids); // invalid ids and groups of one are dropped
}

void SpatialProcessor::setSourceGroups (const SourceGroups& groups)
{
    std::vector<std::pair<juce::String, float>> changes;
    for (int i = 0; i < kMaxChannels; ++i)
        changes.emplace_back (params::sourceGroupId (i), (float) groups.groupOf (i));

    writeParameters (changes);
}

void SpatialProcessor::migrateLegacyState()
{
    auto& state = apvts.state;

    // Sessions saved by earlier versions kept these in the state, not in parameters.
    if (state.hasProperty (kSourceGroups))
    {
        const auto groups = SourceGroups::fromString (state.getProperty (kSourceGroups).toString().toStdString());
        state.removeProperty (kSourceGroups, nullptr);
        std::vector<std::pair<juce::String, float>> changes;
        for (int i = 0; i < kMaxChannels; ++i)
            changes.emplace_back (params::sourceGroupId (i), (float) groups.groupOf (i));
        writeParameters (changes, false);
    }

    if (state.hasProperty (kExcludedSpeakers))
    {
        const auto text = state.getProperty (kExcludedSpeakers).toString();
        state.removeProperty (kExcludedSpeakers, nullptr);
        std::vector<std::pair<juce::String, float>> changes;
        for (int i = 0; i < kMaxChannels; ++i)
            changes.emplace_back (params::speakerExcludeId (i), i < text.length() && text[i] == '1' ? 1.0f : 0.0f);
        writeParameters (changes, false);
    }

    if (state.hasProperty (kVirtualMode) || state.hasProperty (kManualVirtual))
    {
        const auto mode = (int) state.getProperty (kVirtualMode, (int) VirtualMode::Auto);
        const auto tokens = juce::StringArray::fromTokens (state.getProperty (kManualVirtual).toString(), " ", "");
        state.removeProperty (kVirtualMode, nullptr);
        state.removeProperty (kManualVirtual, nullptr);

        if (hasVirtualParameters)
        {
            std::vector<Vec2> list;
            for (int i = 0; i + 1 < tokens.size() && (int) list.size() < (int) kMaxVirtualSpeakers; i += 2)
            {
                const double x = tokens[i].getDoubleValue(), y = tokens[i + 1].getDoubleValue();
                if (std::isfinite (x) && std::isfinite (y))
                    list.push_back ({ x, y });
            }

            setManualVirtualSpeakers (list, false);
            writeParameters ({ { params::kVirtualModeId, (float) (juce::jlimit (1, 3, mode) - 1) } }, false);
        }
    }
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
            migrateLegacyState();
            osc->configure (getOscSettings());
            stateRestored();
        }
}
}

namespace tlos
{
/** Raises `slot` to `value` if it is higher (only the audio thread raises; the message thread resets). */
static void raisePeak (std::atomic<float>& slot, float value) noexcept
{
    if (value > slot.load (std::memory_order_relaxed))
        slot.store (value, std::memory_order_relaxed);
}

void SpatialProcessor::recordInputLevels (const juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    const int count = std::min ({ numChannels, buffer.getNumChannels(), kMaxChannels });
    for (int ch = 0; ch < count; ++ch)
        raisePeak (inputPeaks[(size_t) ch], buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
}

void SpatialProcessor::recordOutputLevels (const juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    const int count = std::min ({ numChannels, buffer.getNumChannels(), kMaxChannels });
    for (int ch = 0; ch < count; ++ch)
        raisePeak (outputPeaks[(size_t) ch], buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
}
}
