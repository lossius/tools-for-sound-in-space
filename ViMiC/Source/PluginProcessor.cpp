// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginProcessor.cpp
    @brief Microphone and reflection bank publishing and the real-time rendering loop of ViMiC. */

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

/** The slowest speed of sound the delay lines have to allow for (at -20 degrees Celsius), in m/s. */
constexpr double kSlowestSound = 331.3 + 0.606 * -20.0;

/** @return The IDs of the model parameters, in the order of vimic::params::Global. */
const std::vector<juce::String>& globalIds()
{
    static const std::vector<juce::String> ids = []
    {
        namespace p = vimic::params;
        std::vector<juce::String> v { p::kDistanceLawId, p::kDistancePowerId, p::kDistanceDbId, p::kMinDistanceId, p::kMicPowerId, p::kMicPolarityId,
                                      p::kSourcePowerId, p::kDelayModeId, p::kFadeThresholdId, p::kFadeTimeId, p::kTemperatureId, p::kDirectGainId,
                                      p::kOutputGainId, p::kRoomWidthId, p::kRoomDepthId, p::kRoomHeightId, p::kReflOrderId, p::kRefl1GainId,
                                      p::kRefl2GainId, p::kThresholdId, p::kAirCutoffId, p::kLowMidId, p::kMidHighId };
        for (int wall = 0; wall < vimic::WallCount; ++wall)
            for (int band = 0; band < 3; ++band)
                v.push_back (p::absorptionId (wall, band));
        jassert (v.size() == p::GlobalCount);
        return v;
    }();
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
        srcYawParam[(size_t) i] = apvts.getRawParameterValue (vimic::params::srcYawId (i));
        srcPatternParam[(size_t) i] = apvts.getRawParameterValue (vimic::params::srcPatternId (i));
        yawParam[(size_t) i] = apvts.getRawParameterValue (vimic::params::micYawId (i));
        patternParam[(size_t) i] = apvts.getRawParameterValue (vimic::params::micPatternId (i));
        srcHeightParam[(size_t) i] = apvts.getRawParameterValue (vimic::params::srcHeightId (i));
        srcPitchParam[(size_t) i] = apvts.getRawParameterValue (vimic::params::srcPitchId (i));
        heightParam[(size_t) i] = apvts.getRawParameterValue (vimic::params::micHeightId (i));
        pitchParam[(size_t) i] = apvts.getRawParameterValue (vimic::params::micPitchId (i));
    }

    const std::size_t paths = (std::size_t) tlos::kMaxChannels * tlos::kMaxChannels * kMaxImages;
    for (auto* v : { &prevGains, &panGains, &targetGains, &prevDelays, &delays, &targetDelays, &heldDelay, &fadeFrom })
        v->assign (paths, 0.0f);
    fadeAt.assign (paths, -1);

    setLatencySamples ((int) vimic::DelayLine::kMinDelay);
}

std::vector<tlos::OscParameter> ViMiCAudioProcessor::getOscParameters() const
{
    std::vector<juce::String> addresses { "/vimic/law", "/vimic/power", "/vimic/db", "/vimic/mindist", "/vimic/micpower", "/vimic/polarity",
                                          "/vimic/sourcepower", "/vimic/delaymode", "/vimic/fadethreshold", "/vimic/fadetime", "/vimic/temperature",
                                          "/vimic/directgain", "/vimic/gain", "/vimic/roomwidth", "/vimic/roomdepth", "/vimic/roomheight",
                                          "/vimic/reflorder", "/vimic/refl1gain", "/vimic/refl2gain", "/vimic/threshold", "/vimic/aircutoff",
                                          "/vimic/lowmid", "/vimic/midhigh" };
    for (int wall = 0; wall < vimic::WallCount; ++wall)
        for (int band = 0; band < 3; ++band)
            addresses.push_back ("/vimic/" + vimic::params::absorptionId (wall, band).toLowerCase());

    std::vector<tlos::OscParameter> out;
    for (size_t i = 0; i < globalParam.size(); ++i)
        out.push_back ({ addresses[i], globalIds()[i] });
    return out;
}

std::vector<tlos::SpeakerColumn> ViMiCAudioProcessor::getSpeakerColumns() const
{
    const auto yawTitle = "Yaw (" + juce::String::charToString (0xb0) + ")";
    const auto pitchTitle = "Pitch (" + juce::String::charToString (0xb0) + ")";
    return { tlos::SpeakerColumn { "Height (m)", "z", [] (int i) { return vimic::params::micHeightId (i); }, 0.0, (double) vimic::params::kMaxHeight, (double) vimic::params::kDefaultHeight, 2 },
             tlos::SpeakerColumn { yawTitle, "yaw", [] (int i) { return vimic::params::micYawId (i); }, -180.0, 180.0, 0.0, 1 },
             tlos::SpeakerColumn { pitchTitle, "pitch", [] (int i) { return vimic::params::micPitchId (i); }, -90.0, 90.0, 0.0, 1 },
             tlos::SpeakerColumn { "Pattern", "pattern", [] (int i) { return vimic::params::micPatternId (i); }, 0.0, 1.0, 0.5, 2 } };
}

std::vector<tlos::SpeakerColumn> ViMiCAudioProcessor::getSourceColumns() const
{
    const auto yawTitle = "Yaw (" + juce::String::charToString (0xb0) + ")";
    const auto pitchTitle = "Pitch (" + juce::String::charToString (0xb0) + ")";
    return { tlos::SpeakerColumn { "Height (m)", "z", [] (int i) { return vimic::params::srcHeightId (i); }, 0.0, (double) vimic::params::kMaxHeight, (double) vimic::params::kDefaultHeight, 2 },
             tlos::SpeakerColumn { yawTitle, "yaw", [] (int i) { return vimic::params::srcYawId (i); }, -180.0, 180.0, 0.0, 1 },
             tlos::SpeakerColumn { pitchTitle, "pitch", [] (int i) { return vimic::params::srcPitchId (i); }, -90.0, 90.0, 0.0, 1 },
             tlos::SpeakerColumn { "Pattern", "pattern", [] (int i) { return vimic::params::srcPatternId (i); }, 0.0, 1.0, 1.0, 2 } };
}

vimic::Source ViMiCAudioProcessor::getSource (int index) const noexcept
{
    index = std::clamp (index, 0, tlos::kMaxChannels - 1);
    return vimic::Source ({ (double) srcX[(size_t) index]->load(), (double) srcY[(size_t) index]->load() },
                          (double) srcYawParam[(size_t) index]->load() * kDegreesToRadians, (double) srcPatternParam[(size_t) index]->load(),
                          (double) srcHeightParam[(size_t) index]->load(), (double) srcPitchParam[(size_t) index]->load() * kDegreesToRadians);
}

vimic::PathSettings ViMiCAudioProcessor::getPathSettings() const noexcept
{
    namespace g = vimic::params;
    const auto v = [this] (size_t i) { return (double) globalParam[i]->load(); };

    vimic::PathSettings s;
    s.law = v (g::Law) >= 0.5 ? vimic::DistanceLaw::Exponential : vimic::DistanceLaw::Inverse;
    s.power = v (g::Power);
    s.dbPerMetre = v (g::Db);
    s.minDistance = v (g::MinDistance);
    s.micPower = (int) std::lround (v (g::MicPower));
    s.restrictPolarity = v (g::Polarity) >= 0.5;
    s.sourcePower = (int) std::lround (v (g::SourcePower));
    s.temperatureCelsius = v (g::Temperature);
    return s;
}

vimic::RoomSettings ViMiCAudioProcessor::getRoomSettings() const noexcept
{
    namespace g = vimic::params;
    const auto v = [this] (size_t i) { return globalParam[i]->load(); };

    vimic::RoomSettings r;
    r.room = { (double) v (g::RoomWidth), (double) v (g::RoomDepth), (double) v (g::RoomHeight) };
    r.order = std::clamp ((int) std::lround (v (g::ReflOrder)), 0, vimic::kMaxOrder);
    r.gainFirst = (double) juce::Decibels::decibelsToGain (v (g::Refl1Gain), g::kMinGainDb);
    r.gainSecond = (double) juce::Decibels::decibelsToGain (v (g::Refl2Gain), g::kMinGainDb);
    r.threshold = v (g::RenderThreshold) <= g::kThresholdOff + 0.05f ? 0.0 : (double) juce::Decibels::decibelsToGain (v (g::RenderThreshold));
    return r;
}

vimic::Walls ViMiCAudioProcessor::getWalls() const noexcept
{
    namespace g = vimic::params;
    vimic::Walls walls;
    for (int wall = 0; wall < vimic::WallCount; ++wall)
        walls.surface[(size_t) wall] = { (double) globalParam[g::AbsorptionFirst + (size_t) (3 * wall)]->load(),
                                         (double) globalParam[g::AbsorptionFirst + (size_t) (3 * wall + 1)]->load(),
                                         (double) globalParam[g::AbsorptionFirst + (size_t) (3 * wall + 2)]->load() };
    walls.lowMidHz = (double) globalParam[g::LowMid]->load();
    walls.midHighHz = (double) globalParam[g::MidHigh]->load();
    return walls;
}

double ViMiCAudioProcessor::getAirCutoff() const noexcept
{
    return (double) globalParam[vimic::params::AirCutoff]->load();
}

vimic::params::DelayMode ViMiCAudioProcessor::getDelayMode() const noexcept
{
    return static_cast<vimic::params::DelayMode> (std::clamp ((int) std::lround (globalParam[vimic::params::DelayModeIndex]->load()), 0, 3));
}

std::vector<vimic::Microphone> ViMiCAudioProcessor::getMicrophones (const tlos::IncludedSpeakers& speakers) const
{
    std::vector<vimic::Microphone> out;
    out.reserve (speakers.positions.size());

    for (size_t i = 0; i < speakers.positions.size(); ++i)
    {
        const int channel = speakers.channelOf[i];
        out.push_back ({ speakers.positions[i], (double) yawParam[(size_t) channel]->load() * kDegreesToRadians,
                         (double) patternParam[(size_t) channel]->load(), (double) heightParam[(size_t) channel]->load(),
                         (double) pitchParam[(size_t) channel]->load() * kDegreesToRadians });
    }

    return out;
}

void ViMiCAudioProcessor::layoutWasReset (bool sourcesReset, bool speakersReset)
{
    // Microphones point outwards: the yaw is the azimuth of the position (counter-clockwise from the front).
    if (speakersReset)
        for (int i = 0; i < getActiveSpeakers(); ++i)
        {
            const double x = (double) spkX[(size_t) i]->load(), y = (double) spkY[(size_t) i]->load();
            if (std::hypot (x, y) > 1e-9)
                setParameterValue (vimic::params::micYawId (i), (float) (std::atan2 (-x, y) / kDegreesToRadians));
        }

    // Sources face the centre (the listener, the middle of the microphones): the azimuth of the direction from the source to the origin.
    if (sourcesReset)
        for (int i = 0; i < getActiveSources(); ++i)
        {
            const double x = (double) srcX[(size_t) i]->load(), y = (double) srcY[(size_t) i]->load();
            if (std::hypot (x, y) > 1e-9)
                setParameterValue (vimic::params::srcYawId (i), (float) (std::atan2 (x, -y) / kDegreesToRadians));
        }
}

void ViMiCAudioProcessor::refreshLayout()
{
    const auto mics = getIncludedSpeakers();

    const auto same = [] (const tlos::Vec2& a, const tlos::Vec2& b) { return juce::exactlyEqual (a.x, b.x) && juce::exactlyEqual (a.y, b.y); };

    if (! (havePublished && mics.channelOf == publishedMics.channelOf
           && mics.positions.size() == publishedMics.positions.size()
           && std::equal (mics.positions.begin(), mics.positions.end(), publishedMics.positions.begin(), same)))
    {
        auto set = std::make_unique<MicSet>();
        set->positions = mics.positions;
        set->channelOf = mics.channelOf;
        set->id = nextSetId++;
        exchange.publish (std::move (set));

        publishedMics = mics;
        havePublished = true;
    }

    refreshBank();
}

void ViMiCAudioProcessor::refreshBank()
{
    const auto room = getRoomSettings();
    const double rate = preparedSampleRate.load();
    const int block = preparedBlock.load();
    if (room.order == 0 || rate <= 0.0 || block <= 0)
        return;

    const int sources = std::min (getTotalNumInputChannels(), tlos::kMaxChannels);
    const int classes = vimic::filterClassCount (room.order) - 1;

    // The longest path of an image of at most the second order is shorter than three times the diagonal of the room (even for a source or a microphone in a corner).
    const double diagonal = std::sqrt (room.room.width * room.room.width + room.room.depth * room.room.depth + room.room.height * room.room.height);
    const int needed = (int) std::ceil (3.0 * diagonal / kSlowestSound * rate) + 16;

    const bool fits = bankInfo.valid && ! bankStale.load() && bankInfo.sources == sources && bankInfo.classes >= classes && bankInfo.capacity >= needed
                   && bankInfo.block == block && juce::exactlyEqual (bankInfo.sampleRate, rate);
    if (fits || sources == 0)
        return;

    // Room for a quarter more than needed, so that a room that grows a little does not make a new bank each time.
    const int capacity = needed + needed / 4;
    const int allClasses = vimic::filterClassCount (vimic::kMaxOrder) - 1;
    auto bank = std::make_unique<ReflectionBank>();
    bank->sources = sources;
    bank->classes = std::max (classes, std::min (allClasses, std::max (classes, bankInfo.classes)));
    bank->capacity = capacity;
    bank->lines.assign ((size_t) (sources * bank->classes), vimic::DelayLine());
    for (auto& line : bank->lines)
        line.prepare (capacity, block);
    bank->splitters.assign ((size_t) sources, vimic::BandSplitter());

    bankInfo = { true, sources, bank->classes, capacity, block, rate, (std::size_t) sources * (std::size_t) bank->classes * (std::size_t) (capacity + block + 8) };
    bankStale = false;
    bankExchange.publish (std::move (bank));
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
    for (auto* v : { &bandLow, &bandMid, &bandHigh })
        v->assign ((size_t) block, 0.0f);

    // One second of every source: the longest direct path at +-100 m is about 0.8 s.
    lines.assign ((size_t) std::max (n, 1), vimic::DelayLine());
    for (auto& line : lines)
        line.prepare ((int) std::ceil (sampleRate) + 8, block);

    usedSettings.valid = false;
    for (auto& key : pathKeys)
        key.valid = false;
    restartRamp = true;

    // The reflection bank depends on these; the message thread makes it (it allocates).
    preparedSampleRate = sampleRate;
    preparedBlock = block;
    bankStale = true;

    // First run only: give the active channels a sensible default layout.
    initialiseLayoutIfFirstRun();
}

void ViMiCAudioProcessor::releaseResources()
{
    exchange.release();
    bankExchange.release();
}

void ViMiCAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    namespace g = vimic::params;

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
    const ReflectionBank* bank = bankExchange.acquire();
    const int usedSources = getActiveSources();
    const int usedSpeakers = getActiveSpeakers();
    const int activeSources = std::min (n, usedSources);

    // The settings and the microphones' orientation, read every block: a change recomputes the paths of every source.
    const auto pathSettings = getPathSettings();
    const auto roomSettings = getRoomSettings();
    const auto delayMode = getDelayMode();
    const bool absolute = g::isAbsolute (delayMode);
    const bool crossFade = g::isCrossFade (delayMode);

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
        settings.height[(size_t) k] = heightParam[(size_t) channel]->load();
        settings.pitch[(size_t) k] = pitchParam[(size_t) channel]->load();
        micArray[(size_t) k] = { mics->positions[(size_t) k], (double) settings.yaw[(size_t) k] * kDegreesToRadians, (double) settings.pattern[(size_t) k],
                                 (double) settings.height[(size_t) k], (double) settings.pitch[(size_t) k] * kDegreesToRadians };
    }

    // Only what the paths depend on (not the absorption, the crossovers and the air cut-off, which only change the filters) recomputes them.
    const auto equal = [] (float p, float q) { return juce::exactlyEqual (p, q); };
    const bool sameSettings = usedSettings.valid && usedSettings.setId == settings.setId && juce::exactlyEqual (usedSettings.sampleRate, settings.sampleRate)
                           && std::equal (usedSettings.globals.begin(), usedSettings.globals.begin() + (long) g::FilterFirst, settings.globals.begin(), equal)
                           && std::equal (usedSettings.yaw.begin(), usedSettings.yaw.begin() + micCount, settings.yaw.begin(), equal)
                           && std::equal (usedSettings.pattern.begin(), usedSettings.pattern.begin() + micCount, settings.pattern.begin(), equal)
                           && std::equal (usedSettings.height.begin(), usedSettings.height.begin() + micCount, settings.height.begin(), equal)
                           && std::equal (usedSettings.pitch.begin(), usedSettings.pitch.begin() + micCount, settings.pitch.begin(), equal);
    if (! sameSettings)
    {
        const bool modeChanged = usedSettings.valid && ! juce::exactlyEqual (usedSettings.globals[g::DelayModeIndex], settings.globals[g::DelayModeIndex]);
        for (auto& key : pathKeys)
            key.valid = false;
        if (modeChanged)
            restartRamp = true;   // a different delay mode is a different set of delays: no sweep from one to the other
    }
    usedSettings = settings;

    // The reflections need a bank that fits: this many sources, the wall sequences of the order, and the memory for the room.
    const int order = roomSettings.order;
    const auto imageTable = vimic::images (vimic::kMaxOrder);
    const int imageCount = (int) vimic::images (order).size();
    const int imageLimit = std::max (imageCount, lastImageCount);          // the paths of a higher order that are fading out are visited too
    const std::size_t used = (std::size_t) n * tlos::kMaxChannels * kMaxImages;   // the rows of the sources in use
    const int classes = vimic::filterClassCount (order) - 1;
    const bool useReflections = order > 0 && bank != nullptr && bank->sources >= n && bank->classes >= classes;
    std::array<vimic::BandGains, 24> newClassGains {};     // the band gains of the wall sequences for this block

    if (useReflections)
    {
        // The splitting of the sources follows the crossovers and the air cut-off (a new design keeps the memory of the filters, so nothing clicks); the gains of
        // the walls in the three bands follow the absorption (the lines are written with gains that ramp to the new ones across the block).
        const bool filtersUpToDate = bank->designed && std::equal (bank->designedFor.begin() + (long) g::FilterFirst, bank->designedFor.end(),
                                                                   settings.globals.begin() + (long) g::FilterFirst, equal);
        if (! filtersUpToDate)
        {
            const auto walls = getWalls();
            for (int s = 0; s < bank->sources; ++s)
                bank->splitters[(size_t) s].design (sampleRate, walls.lowMidHz, walls.midHighHz, getAirCutoff());

            for (int c = 1; c <= bank->classes; ++c)
                newClassGains[(size_t) c - 1] = vimic::bandGains (vimic::classHits (c), walls);

            if (! bank->designed)
                bank->classGains = newClassGains;       // the first time: nothing to ramp from

            bank->designedFor = settings.globals;
            bank->designed = true;
        }
        else
        {
            newClassGains = bank->classGains;
        }
    }

    // The time a delay of 0 is: the smallest delay the interpolation allows (the plugin's latency).
    const double minimum = vimic::DelayLine::kMinDelay;
    const double longest = std::ceil (sampleRate);

    for (int s = 0; s < n; ++s)
    {
        auto& key = pathKeys[(size_t) s];

        if (s >= activeSources || mics == nullptr || micCount == 0)
        {
            key.valid = false;
            std::fill_n (panGains.begin() + (long) at (s, 0, 0), tlos::kMaxChannels * kMaxImages, 0.0f);
            std::fill_n (delays.begin() + (long) at (s, 0, 0), tlos::kMaxChannels * kMaxImages, 0.0f);
            continue;
        }

        // A source that has not moved, with unchanged microphones and settings, keeps its paths.
        const float x = srcX[(size_t) s]->load(), y = srcY[(size_t) s]->load();
        const float yaw = srcYawParam[(size_t) s]->load(), pattern = srcPatternParam[(size_t) s]->load();
        const float height = srcHeightParam[(size_t) s]->load(), pitch = srcPitchParam[(size_t) s]->load();
        if (key.valid && juce::exactlyEqual (key.x, x) && juce::exactlyEqual (key.y, y) && juce::exactlyEqual (key.yaw, yaw) && juce::exactlyEqual (key.pattern, pattern)
            && juce::exactlyEqual (key.height, height) && juce::exactlyEqual (key.pitch, pitch))
            continue;

        const vimic::Source source ({ (double) x, (double) y }, (double) yaw * kDegreesToRadians, (double) pattern, (double) height, (double) pitch * kDegreesToRadians);

        // Relative delays: only the differences from the shortest direct path (the reflections come later than the direct sound); absolute: the true time of flight.
        double shortest = std::numeric_limits<double>::max();
        if (! absolute)
            for (int k = 0; k < micCount; ++k)
                shortest = std::min (shortest, vimic::computeDirectPath (source, micArray[(size_t) k], pathSettings).delaySeconds);
        else
            shortest = 0.0;

        std::fill_n (panGains.begin() + (long) at (s, 0, 0), tlos::kMaxChannels * kMaxImages, 0.0f);
        std::fill_n (delays.begin() + (long) at (s, 0, 0), tlos::kMaxChannels * kMaxImages, 0.0f);

        for (int k = 0; k < micCount; ++k)
        {
            const int channel = mics->channelOf[(size_t) k];
            for (int i = 0; i < imageCount; ++i)
            {
                const auto path = vimic::computeImagePath (source, micArray[(size_t) k], imageTable[(size_t) i], pathSettings, roomSettings);
                panGains[at (s, channel, i)] = (float) path.gain;
                delays[at (s, channel, i)] = (float) std::clamp ((path.delaySeconds - shortest) * sampleRate + minimum, minimum, longest * 8.0);
            }
        }

        key = { x, y, yaw, pattern, height, pitch, true };
    }

    // Trim, mute and solo of the sources and microphones, the gain of the direct sound and the output gain scale the path gains. Reflections are silent until
    // there is a bank to read them from.
    tlos::ChannelGains channelGains;
    getChannelGains (channelGains);
    const float outputGain = juce::Decibels::decibelsToGain (settings.globals[g::OutputGain], (float) g::kMinGainDb);
    const float directGain = juce::Decibels::decibelsToGain (settings.globals[g::DirectGain], (float) g::kMinGainDb);

    for (int s = 0; s < n; ++s)
        for (int k = 0; k < m; ++k)
        {
            const float level = channelGains.sources[(size_t) s] * channelGains.speakers[(size_t) k] * outputGain;
            for (int i = 0; i < imageLimit; ++i)
            {
                const auto index = at (s, k, i);
                targetGains[index] = panGains[index] * level * (i == 0 ? directGain : (useReflections ? 1.0f : 0.0f));
            }
        }

    std::copy_n (delays.begin(), (long) used, targetDelays.begin());

    const bool restarted = restartRamp.exchange (false);
    if (restarted)
    {
        std::copy_n (targetGains.begin(), (long) used, prevGains.begin());
        std::copy_n (targetDelays.begin(), (long) used, prevDelays.begin());
        prevUsedSpeakers = usedSpeakers;
    }

    // A path that was silent starts at its new delay (nothing is heard to sweep), and one that becomes silent keeps its last delay.
    for (int s = 0; s < n; ++s)
        for (int k = 0; k < m; ++k)
            for (int i = 0; i < imageLimit; ++i)
            {
                const auto index = at (s, k, i);
                if (juce::exactlyEqual (prevGains[index], 0.0f))
                    prevDelays[index] = targetDelays[index];
                else if (juce::exactlyEqual (targetGains[index], 0.0f))
                    targetDelays[index] = prevDelays[index];
            }

    // Cross-fade mode: a path keeps the delay it is rendered with until the new one is further away than the threshold, and then fades to the new one. A path that was
    // silent starts at its new delay; a restart (or a switch of the mode) takes every path to its new delay.
    const int fadeLength = std::max (16, (int) std::lround ((double) settings.globals[g::FadeTime] * 0.001 * sampleRate));
    if (crossFade)
    {
        const float threshold = (float) ((double) settings.globals[g::FadeThreshold] * 0.001 * sampleRate);
        for (int s = 0; s < n; ++s)
            for (int k = 0; k < m; ++k)
                for (int i = 0; i < imageLimit; ++i)
                {
                    const auto index = at (s, k, i);
                    if (juce::exactlyEqual (prevGains[index], 0.0f) || restarted)
                    {
                        heldDelay[index] = targetDelays[index];
                        fadeAt[index] = -1;
                    }
                    else if (fadeAt[index] < 0 && std::abs (targetDelays[index] - heldDelay[index]) > threshold)
                    {
                        fadeFrom[index] = heldDelay[index];
                        heldDelay[index] = targetDelays[index];
                        fadeAt[index] = 0;
                    }
                }
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
            const float* in = inputCopy.getReadPointer (s);
            line.write (in, len);

            // The filtered copies of the source for the walls its reflections hit: the three bands of the source with the gains of the walls (ramping to a new
            // absorption across the block).
            if (useReflections)
            {
                bank->splitters[(size_t) s].process (in, bandLow.data(), bandMid.data(), bandHigh.data(), len);
                for (int c = 1; c <= classes; ++c)
                {
                    const auto& was = bank->classGains[(size_t) c - 1];
                    const auto& now = newClassGains[(size_t) c - 1];
                    const float from[3] { (float) (was.low + (now.low - was.low) * f0), (float) (was.mid + (now.mid - was.mid) * f0), (float) (was.high + (now.high - was.high) * f0) };
                    const float to[3] { (float) (was.low + (now.low - was.low) * f1), (float) (was.mid + (now.mid - was.mid) * f1), (float) (was.high + (now.high - was.high) * f1) };
                    bank->lines[(size_t) (s * bank->classes + c - 1)].writeMix (bandLow.data(), bandMid.data(), bandHigh.data(), from, to, len);
                }
            }

            // The previous gains may belong to a larger set of microphones, so go through every output.
            for (int k = 0; k < speakerLimit; ++k)
            {
                float* out = buffer.getWritePointer (k) + start;

                for (int i = 0; i < imageLimit; ++i)
                {
                    const auto index = at (s, k, i);
                    const float g0 = prevGains[index], g1 = targetGains[index];
                    if (juce::exactlyEqual (g0, 0.0f) && juce::exactlyEqual (g1, 0.0f))
                        continue;

                    // The direct sound is read from the source's own line, a reflection from the filtered copy of its wall sequence.
                    const vimic::DelayLine* tapLine = &line;
                    if (i > 0)
                    {
                        const int c = imageTable[(size_t) i].filterClass;
                        if (bank == nullptr || s >= bank->sources || c > bank->classes)
                            continue;
                        tapLine = &bank->lines[(size_t) (s * bank->classes + c - 1)];
                    }

                    const float chunkGain0 = g0 + (g1 - g0) * f0, chunkGain1 = g0 + (g1 - g0) * f1;

                    if (! crossFade)
                    {
                        const float d0 = prevDelays[index], d1 = targetDelays[index];
                        tapLine->addTap (out, len, (double) (d0 + (d1 - d0) * f0), (double) (d0 + (d1 - d0) * f1), chunkGain0, chunkGain1);
                        continue;
                    }

                    // Cross-fade: one tap at the delay in use, or two while the path fades from the old delay to the new one (the weights ramp linearly across the chunk).
                    const int at0 = fadeAt[index];
                    if (at0 < 0)
                    {
                        tapLine->addTap (out, len, (double) heldDelay[index], (double) heldDelay[index], chunkGain0, chunkGain1);
                    }
                    else
                    {
                        const float p0 = std::min (1.0f, (float) at0 / (float) fadeLength), p1 = std::min (1.0f, (float) (at0 + len) / (float) fadeLength);
                        tapLine->addTap (out, len, (double) fadeFrom[index], (double) fadeFrom[index], chunkGain0 * (1.0f - p0), chunkGain1 * (1.0f - p1));
                        tapLine->addTap (out, len, (double) heldDelay[index], (double) heldDelay[index], chunkGain0 * p0, chunkGain1 * p1);
                        fadeAt[index] = at0 + len >= fadeLength ? -1 : at0 + len;
                    }
                }
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
            float gain = 1.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                data[i] *= gain;
                gain -= step;
            }
        }
        else
        {
            buffer.clear (ch, 0, numSamples);
        }
    }
    prevUsedSpeakers = usedSpeakers;

    if (useReflections)
        bank->classGains = newClassGains;      // the next block ramps from here

    std::copy_n (targetGains.begin(), (long) used, prevGains.begin());
    std::copy_n (targetDelays.begin(), (long) used, prevDelays.begin());
    lastImageCount = imageCount;

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
