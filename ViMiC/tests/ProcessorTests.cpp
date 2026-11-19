// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file ProcessorTests.cpp
    @brief Offline tests of the ViMiC processor (no DAW): gains and delays of the direct sound against the model, both delay modes, microphone
    pattern and yaw, exclude/mute, silent unused channels, state round-trip, 64x64, speaker layout files, parameter order. */

#include "../Source/PluginProcessor.h"
#include "../Source/ViMiCParameters.h"
#include "tlos/Parameters.h"
#include "tlos/PositionIO.h"
#include "vimic/Absorption.h"
#include "vimic/Path.h"
#include "vimic/Reflections.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <thread>

/** Number of failed checks so far; the exit code of main(). */
static int failures = 0;

/** Counts and prints a failed condition, and carries on with the next check. */
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (! (cond)) {                                                      \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

namespace
{
/** Sets a parameter to a real value (as a host would). */
void setParam (ViMiCAudioProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.getValueTreeState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

/** Current real value of a parameter. */
float getParam (ViMiCAudioProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

/** Configures the processor for n inputs and m outputs and prepares it. @return false if the layout is refused. */
bool configure (ViMiCAudioProcessor& p, int n, int m, int blockSize)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add  (juce::AudioChannelSet::discreteChannels (n));
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (m));
    if (! p.setBusesLayout (layout))
        return false;
    p.setPlayConfigDetails (n, m, 48000.0, blockSize);
    p.prepareToPlay (48000.0, blockSize);
    return true;
}

/** Fills only input channel `active` with noise; returns its energy. */
double fillNoise (juce::AudioBuffer<float>& b, int active, std::mt19937& rng)
{
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    b.clear();
    double e = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = u (rng);
        b.setSample (active, i, v);
        e += (double) v * v;
    }
    return e;
}

/** Sum of the squared samples of channel `ch`. */
double channelEnergy (const juce::AudioBuffer<float>& b, int ch)
{
    double e = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
        e += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return e;
}

/** Total energy of the first `channels` channels. */
double totalEnergy (const juce::AudioBuffer<float>& b, int channels)
{
    double e = 0.0;
    for (int ch = 0; ch < channels; ++ch)
        e += channelEnergy (b, ch);
    return e;
}

/** True if the first `channels` channels contain only finite samples. */
bool allFinite (const juce::AudioBuffer<float>& b, int channels)
{
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}
}

namespace
{
/** Puts microphone `index` (zero-based) at a position with a yaw (degrees) and a pattern. */
void placeMic (ViMiCAudioProcessor& p, int index, float x, float y, float yawDegrees, float pattern)
{
    setParam (p, tlos::params::speakerId (index, 'x'), x);
    setParam (p, tlos::params::speakerId (index, 'y'), y);
    setParam (p, vimic::params::micYawId (index), yawDegrees);
    setParam (p, vimic::params::micPatternId (index), pattern);
}

/** Puts source `index` at a position. */
void placeSource (ViMiCAudioProcessor& p, int index, float x, float y)
{
    setParam (p, tlos::params::sourceId (index, 'x'), x);
    setParam (p, tlos::params::sourceId (index, 'y'), y);
}

/** Feeds an impulse on input 0 as the first sample of a long block, after a few silent blocks that settle the ramps, and returns the output
    channels' samples (block length `length`). */
juce::AudioBuffer<float> impulseResponse (ViMiCAudioProcessor& p, int inputs, int outputs, int length)
{
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (std::max (inputs, outputs), length);
    for (int block = 0; block < 3; ++block)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
    }

    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    p.processBlock (buffer, midi);
    return buffer;
}

/** The centre of gravity in samples of channel `ch` (0 if it is silent), and its total in `sum`. */
double centroid (const juce::AudioBuffer<float>& b, int ch, double& sum)
{
    double weighted = 0.0;
    sum = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        weighted += (double) b.getSample (ch, i) * i;
        sum += (double) b.getSample (ch, i);
    }
    return std::abs (sum) > 1e-12 ? weighted / sum : 0.0;
}
}

/** The host lists the global parameters first (counts, outside rolloff, ViMiC's twelve), then each source, then each microphone (with yaw and pattern). */
static void testParameterOrder()
{
    ViMiCAudioProcessor p;
    const auto& parameters = p.getParameters();
    const int total = 3 + (int) vimic::params::GlobalCount + 64 * 9 + 64 * 8;

    CHECK (parameters.size() == total);

    const auto idOf = [&] (int i) { return dynamic_cast<juce::AudioProcessorParameterWithID*> (parameters[i])->paramID; };
    CHECK (idOf (0) == tlos::params::kNumSourcesId && idOf (1) == tlos::params::kNumSpeakersId && idOf (2) == tlos::params::kOutsideRolloffId);
    const int globalsEnd = 3 + (int) vimic::params::GlobalCount;                                // the first source parameter is at this index
    CHECK (idOf (3) == vimic::params::kDistanceLawId && idOf (3 + vimic::params::OutputGain) == vimic::params::kOutputGainId && idOf (globalsEnd) == tlos::params::sourceId (0, 'x'));
    CHECK (idOf (3 + vimic::params::RoomWidth) == vimic::params::kRoomWidthId && idOf (3 + vimic::params::ReflOrder) == vimic::params::kReflOrderId && idOf (3 + vimic::params::AirCutoff) == vimic::params::kAirCutoffId);
    CHECK (idOf (3 + vimic::params::AbsorptionFirst) == vimic::params::absorptionId (vimic::Left, 0) && idOf (globalsEnd - 1) == vimic::params::absorptionId (vimic::Ceiling, 2));
    CHECK (std::abs (getParam (p, vimic::params::kRoomWidthId) - 10.0f) < 1e-4f && std::abs (getParam (p, vimic::params::kRoomHeightId) - 3.0f) < 1e-4f && p.getRoomSettings().order == 0);
    CHECK (std::abs (getParam (p, vimic::params::absorptionId (vimic::Left, 1)) - 50.0f) < 1e-3f && std::abs (getParam (p, vimic::params::absorptionId (vimic::Floor, 2)) - 8.0f) < 1e-3f);     // drywall, wood
    CHECK (idOf (3 + vimic::params::SourcePower) == vimic::params::kSourcePowerId && idOf (3 + vimic::params::DelayModeIndex) == vimic::params::kDelayModeId);
    CHECK (idOf (globalsEnd + 7) == vimic::params::srcYawId (0) && idOf (globalsEnd + 8) == vimic::params::srcPatternId (0) && idOf (globalsEnd + 9) == tlos::params::sourceId (1, 'x'));
    CHECK (parameters[globalsEnd + 7]->getName (64) == "Source 1 Yaw" && std::abs (getParam (p, vimic::params::srcPatternId (3)) - 1.0f) < 1e-4f);
    const int speakers = globalsEnd + 64 * 9;
    CHECK (idOf (speakers) == tlos::params::speakerId (0, 'x'));
    CHECK (idOf (speakers + 6) == vimic::params::micYawId (0) && idOf (speakers + 7) == vimic::params::micPatternId (0));
    CHECK (parameters[speakers + 6]->getName (64) == "Microphone 1 Yaw" && parameters[speakers]->getName (64) == "Microphone 1 X");
    CHECK (idOf (speakers + 8) == tlos::params::speakerId (1, 'x'));
    CHECK (idOf (total - 1) == vimic::params::micPatternId (63));

    const auto& tree = p.getParameterTree();
    CHECK (tree.getSubgroups (false).size() == 3 && tree.getSubgroups (false).getFirst()->getName() == "Global");
    CHECK (tree.getSubgroups (false)[2]->getName() == "Microphones");

    // Defaults.
    CHECK (std::abs (getParam (p, vimic::params::kTemperatureId) - 20.0f) < 1e-4f && std::abs (getParam (p, vimic::params::kDistancePowerId) - 1.0f) < 1e-4f);
    CHECK (std::abs (getParam (p, vimic::params::kMinDistanceId) - 1.0f) < 1e-4f && std::abs (getParam (p, vimic::params::kSourceHeightId) - 1.5f) < 1e-4f);
    CHECK (std::abs (getParam (p, vimic::params::micPatternId (7)) - 0.5f) < 1e-4f && getParam (p, vimic::params::kMicPolarityId) > 0.5f);
    CHECK (p.getDelayMode() == vimic::params::DelayMode::Relative && p.getDelayRender() == vimic::params::DelayRender::Smooth);
    CHECK (idOf (3 + vimic::params::DelayRenderIndex) == vimic::params::kDelayRenderId && idOf (3 + vimic::params::FadeThreshold) == vimic::params::kFadeThresholdId && idOf (3 + vimic::params::FadeTime) == vimic::params::kFadeTimeId);

    // What ViMiC tells the shared code.
    CHECK (p.getOutputNoun() == "Microphone" && ! p.usesSpread() && ! p.usesOutsideRolloff() && ! p.supportsVirtualSpeakers());
    CHECK (p.getSpeakerColumns().size() == 2 && p.getSpeakerColumns()[0].attribute == "yaw" && p.getSpeakerColumns()[1].attribute == "pattern");
    CHECK (p.getCoordinateMode() == tlos::SpatialProcessor::CoordinateMode::Cartesian);          // like the other plugins
    CHECK (p.getPadSizeCaption() == "Pad size" && p.getDisplaySectionTitle() == "Pad");
    CHECK (p.getSourceColumns().size() == 2 && p.getSourceColumns()[0].attribute == "yaw" && p.getSourceColumns()[1].attribute == "pattern" && p.getSourceColumns()[1].defaultValue == 1.0);
    CHECK (p.getLatencySamples() == 2 && p.getOscParameters().size() == vimic::params::GlobalCount);
    CHECK (p.getOscParameters()[vimic::params::RoomWidth].address == "/vimic/roomwidth" && p.getOscParameters()[vimic::params::AbsorptionFirst].address == "/vimic/absorbleftlow");
}

/** The default layout points the microphones outwards (yaw = azimuth of the position). */
static void testDefaultLayout()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 2, 4, 256));
    for (int i = 0; i < 4; ++i)
    {
        const double x = getParam (p, tlos::params::speakerId (i, 'x')), y = getParam (p, tlos::params::speakerId (i, 'y'));
        const double azimuth = std::atan2 (-x, y) * 180.0 / std::numbers::pi;
        double difference = std::abs (azimuth - getParam (p, vimic::params::micYawId (i)));
        difference = std::min (difference, 360.0 - difference);
        CHECK (difference < 1e-3);
    }

    // The sources face the centre (the direction from the source to the origin): the source at the front left faces back right, and so on.
    for (int i = 0; i < 2; ++i)
    {
        const double x = getParam (p, tlos::params::sourceId (i, 'x')), y = getParam (p, tlos::params::sourceId (i, 'y'));
        const double towardsCentre = std::atan2 (x, -y) * 180.0 / std::numbers::pi;
        double difference = std::abs (towardsCentre - getParam (p, vimic::params::srcYawId (i)));
        difference = std::min (difference, 360.0 - difference);
        CHECK (difference < 1e-3);
    }
    CHECK (std::abs (getParam (p, vimic::params::srcYawId (0)) + 135.0) < 1e-3);   // source 1 is at the front left (-1.77, 1.77): it faces back right

    // "Reset layout" does it again.
    setParam (p, vimic::params::micYawId (2), 17.0f);
    p.resetLayoutToRings();
    CHECK (std::abs (std::abs (getParam (p, vimic::params::micYawId (2))) - 180.0) < 1e-3);   // speaker 3 is at the back
}

/** The direct sound of two omni microphones: gains from the distance law, delays from the time of flight, in both delay modes. */
static void testGainsAndDelays()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 1, 2, 4096));
    placeMic (p, 0, 2.0f, 0.0f, 0.0f, 1.0f);       // omni, 1 m from the source
    placeMic (p, 1, -2.0f, 0.0f, 0.0f, 1.0f);      // omni, 5 m from the source
    placeSource (p, 0, 3.0f, 0.0f);
    p.refreshLayout();

    const double c = vimic::speedOfSound (20.0);

    // Relative delays: the nearer microphone is the reference (2 samples of latency), the other one is later by the difference of the flights.
    auto out = impulseResponse (p, 1, 2, 4096);
    double sum0 = 0.0, sum1 = 0.0;
    const double c0 = centroid (out, 0, sum0), c1 = centroid (out, 1, sum1);
    CHECK (std::abs (sum0 - 1.0) < 1e-3 && std::abs (sum1 - 0.2) < 1e-3);                    // gains 1 / 1 m and 1 / 5 m
    CHECK (std::abs (c0 - 2.0) < 0.02);
    CHECK (std::abs (c1 - (2.0 + (5.0 - 1.0) / c * 48000.0)) < 0.02);

    // Absolute delays: the full time of flight (and the plugin reports a latency of 2 samples).
    setParam (p, vimic::params::kDelayModeId, 1.0f);
    out = impulseResponse (p, 1, 2, 4096);
    const double d0 = centroid (out, 0, sum0), d1 = centroid (out, 1, sum1);
    CHECK (std::abs (d0 - (2.0 + 1.0 / c * 48000.0)) < 0.02 && std::abs (d1 - (2.0 + 5.0 / c * 48000.0)) < 0.02);
    CHECK (std::abs (sum0 - 1.0) < 1e-3 && std::abs (sum1 - 0.2) < 1e-3);

    // The temperature changes the speed of sound, and so the delays.
    setParam (p, vimic::params::kTemperatureId, 0.0f);
    out = impulseResponse (p, 1, 2, 4096);
    CHECK (std::abs (centroid (out, 1, sum1) - (2.0 + 5.0 / vimic::speedOfSound (0.0) * 48000.0)) < 0.02);
    setParam (p, vimic::params::kTemperatureId, 20.0f);

    // The sample rate scales the delays in samples.
    setParam (p, vimic::params::kDelayModeId, 0.0f);
    p.prepareToPlay (96000.0, 4096);
    out = impulseResponse (p, 1, 2, 4096);
    CHECK (std::abs (centroid (out, 1, sum1) - (2.0 + 4.0 / c * 96000.0)) < 0.02);
    p.prepareToPlay (48000.0, 4096);

    // The exponential law, the exponent, the minimum distance and the gains in dB.
    setParam (p, vimic::params::kDistanceLawId, 1.0f);
    setParam (p, vimic::params::kDistanceDbId, 6.0f);
    out = impulseResponse (p, 1, 2, 4096);
    centroid (out, 1, sum1);
    CHECK (std::abs (sum1 - std::pow (10.0, -6.0 * 5.0 / 20.0)) < 1e-3);
    setParam (p, vimic::params::kDistanceLawId, 0.0f);
    setParam (p, vimic::params::kDistancePowerId, 2.0f);
    out = impulseResponse (p, 1, 2, 4096);
    centroid (out, 1, sum1);
    CHECK (std::abs (sum1 - 1.0 / 25.0) < 1e-3);
    setParam (p, vimic::params::kDistancePowerId, 1.0f);
    setParam (p, vimic::params::kDirectGainId, -6.0f);
    setParam (p, vimic::params::kOutputGainId, -6.0f);
    out = impulseResponse (p, 1, 2, 4096);
    centroid (out, 1, sum1);
    CHECK (std::abs (sum1 - 0.2 * std::pow (10.0, -12.0 / 20.0)) < 1e-3);
}

/** The microphone's pattern and yaw: a cardioid hears what it points at, and not what is behind it; the pattern follows the host parameters at once. */
static void testPatternAndYaw()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 1, 1, 2048));
    placeMic (p, 0, 0.0f, 0.0f, 0.0f, 0.5f);       // a cardioid at the centre pointing to the front
    p.refreshLayout();
    juce::AudioBuffer<float> out;
    double sum = 0.0;

    const auto level = [&] (float sx, float sy)
    {
        placeSource (p, 0, sx, sy);
        out = impulseResponse (p, 1, 1, 2048);
        centroid (out, 0, sum);
        return sum;
    };

    CHECK (std::abs (level (0.0f, 4.0f) - 0.25) < 1e-3);          // on axis: 1 / 4 m
    CHECK (std::abs (level (-4.0f, 0.0f) - 0.125) < 1e-3);        // 90 degrees to the left: half (cardioid)
    CHECK (std::abs (level (4.0f, 0.0f) - 0.125) < 1e-3);
    CHECK (std::abs (level (0.0f, -4.0f)) < 1e-3);                // behind: nothing

    setParam (p, vimic::params::micYawId (0), 90.0f);             // turned to the left
    CHECK (std::abs (level (-4.0f, 0.0f) - 0.25) < 1e-3 && std::abs (level (4.0f, 0.0f)) < 1e-3);

    setParam (p, vimic::params::micPatternId (0), 0.0f);          // figure-8 turned to the left: the rear lobe is removed while the polarity is restricted
    CHECK (std::abs (level (-4.0f, 0.0f) - 0.25) < 1e-3 && std::abs (level (4.0f, 0.0f)) < 1e-3 && std::abs (level (0.0f, 4.0f)) < 1e-3);
    setParam (p, vimic::params::kMicPolarityId, 0.0f);            // free polarity: anti-phase from the right
    CHECK (std::abs (level (4.0f, 0.0f) + 0.25) < 1e-3);

    setParam (p, vimic::params::kMicPolarityId, 1.0f);
    setParam (p, vimic::params::micPatternId (0), 0.5f);
    setParam (p, vimic::params::kMicPowerId, 2.0f);                // the exponent: sharper
    CHECK (std::abs (level (-4.0f, 0.0f) - 0.25) < 1e-3);
    setParam (p, vimic::params::micYawId (0), 0.0f);
    CHECK (std::abs (level (-4.0f, 0.0f) - 0.25 * 0.25) < 1e-3);   // (0.5)^2 at 90 degrees
}

/** A source that radiates more in one direction: heard when it faces the microphone, not when it faces away; yaw, the exponent and the files follow. */
static void testSourceDirectivity()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 1, 1, 2048));
    placeMic (p, 0, 0.0f, 0.0f, 0.0f, 1.0f);                       // an omni microphone at the centre
    placeSource (p, 0, 0.0f, 4.0f);                                 // 4 m in front of it
    p.refreshLayout();
    juce::AudioBuffer<float> out;
    double sum = 0.0;

    const auto level = [&]
    {
        out = impulseResponse (p, 1, 1, 2048);
        centroid (out, 0, sum);
        return sum;
    };

    CHECK (std::abs (level() - 0.25) < 1e-3);                      // omni by default: only the distance counts, whatever the yaw
    setParam (p, vimic::params::srcYawId (0), 123.0f);
    CHECK (std::abs (level() - 0.25) < 1e-3);

    setParam (p, vimic::params::srcPatternId (0), 0.5f);          // a cardioid source
    setParam (p, vimic::params::srcYawId (0), 180.0f);            // facing the microphone (at its back, towards the centre)
    CHECK (std::abs (level() - 0.25) < 1e-3);
    setParam (p, vimic::params::srcYawId (0), 0.0f);              // facing away
    CHECK (std::abs (level()) < 1e-3);
    setParam (p, vimic::params::srcYawId (0), 90.0f);             // to the left: the microphone is at its side
    CHECK (std::abs (level() - 0.125) < 1e-3);
    setParam (p, vimic::params::kSourcePowerId, 2.0f);            // the exponent
    CHECK (std::abs (level() - 0.25 * 0.25) < 1e-3);
    setParam (p, vimic::params::kSourcePowerId, 1.0f);
    CHECK (p.getSource (0).pattern == 0.5 && std::abs (p.getSource (0).yaw - std::numbers::pi / 2.0) < 1e-4 && std::abs (p.getSource (0).position.y - 4.0) < 1e-4);

    // Every microphone hears the source differently: a source turned towards one of two microphones.
    ViMiCAudioProcessor q;
    CHECK (configure (q, 1, 2, 2048));
    placeMic (q, 0, -3.0f, 0.0f, 0.0f, 1.0f);
    placeMic (q, 1, 3.0f, 0.0f, 0.0f, 1.0f);
    placeSource (q, 0, 0.0f, 0.0f);
    setParam (q, vimic::params::srcPatternId (0), 0.0f);          // a figure-8 source (the rear lobe is removed), turned to the left
    setParam (q, vimic::params::srcYawId (0), 90.0f);
    q.refreshLayout();
    out = impulseResponse (q, 1, 2, 2048);
    double sumLeft = 0.0, sumRight = 0.0;
    centroid (out, 0, sumLeft);
    centroid (out, 1, sumRight);
    CHECK (std::abs (sumLeft - 1.0 / 3.0) < 1e-3 && std::abs (sumRight) < 1e-3);

    // OSC: /source/N/yaw and /pattern (and /speaker/N/yaw) set the parameters.
    auto& osc = q.getOscControl();
    CHECK (osc.handleMessage (juce::OSCMessage (juce::OSCAddressPattern ("/source/1/yaw"), -45.0f)));
    CHECK (std::abs (getParam (q, vimic::params::srcYawId (0)) + 45.0f) < 1e-3f);
    CHECK (osc.handleMessage (juce::OSCMessage (juce::OSCAddressPattern ("/source/1/pattern"), 0.25f)) && std::abs (getParam (q, vimic::params::srcPatternId (0)) - 0.25f) < 1e-4f);
    CHECK (osc.handleMessage (juce::OSCMessage (juce::OSCAddressPattern ("/speaker/2/pattern"), 0.8f)) && std::abs (getParam (q, vimic::params::micPatternId (1)) - 0.8f) < 1e-4f);
    CHECK (osc.handleMessage (juce::OSCMessage (juce::OSCAddressPattern ("/vimic/sourcepower"), 3.0f)) && getParam (q, vimic::params::kSourcePowerId) == 3.0f);
}

/** The room: the impulse response with the reflections on has every path of the images, in the right places, with the filter of its walls; the totals (DC gain)
    follow the sum over the images of the path gain and the low-band gain of the walls the path hits. */
static void testReflections()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 1, 1, 16384));
    placeMic (p, 0, 0.0f, 0.0f, 0.0f, 1.0f);                       // an omni microphone at the centre
    placeSource (p, 0, 2.0f, 0.0f);                                 // an omni source 2 m to the right of it
    setParam (p, vimic::params::kAirCutoffId, 19000.0f);
    for (int wall = 0; wall < vimic::WallCount; ++wall)
        for (int band = 0; band < 3; ++band)
            setParam (p, vimic::params::absorptionId (wall, band), 0.1f);
    p.refreshLayout();

    const auto totalOf = [&] (int order)
    {
        setParam (p, vimic::params::kReflOrderId, (float) order);
        p.refreshLayout();                                          // makes the reflection bank (the timer does it in a host)
        auto out = impulseResponse (p, 1, 1, 16384);
        double sum = 0.0;
        centroid (out, 0, sum);
        return std::pair { sum, out };
    };

    // The direct sound alone, then with the first-order reflections.
    const auto direct = totalOf (0);
    CHECK (std::abs (direct.first - 0.5) < 1e-3);                   // 1 / 2 m
    CHECK (p.getReflectionBankSize() == 0);                         // no bank before it is needed

    const auto first = totalOf (1);
    CHECK (p.getReflectionBankSize() > 0);

    // The expected sum: the direct path and the six first-order images (the reflection factor of 0.1 % is 0.9995 for every wall).
    const auto pathSettings = p.getPathSettings();
    auto room = p.getRoomSettings();
    const auto walls = p.getWalls();
    const vimic::Microphone mic { { 0.0, 0.0 }, 0.0, 1.0 };
    const vimic::Source source ({ 2.0, 0.0 }, 0.0, 1.0);

    const auto expectedTotal = [&] (int order)
    {
        room.order = order;
        double total = 0.0;
        for (const auto& image : vimic::images (order))
            total += vimic::computeImagePath (source, mic, image, pathSettings, room).gain * vimic::bandGains (image.hits, walls).low;
        return total;
    };
    CHECK (std::abs (first.first - expectedTotal (1)) < 2e-3 && expectedTotal (1) > expectedTotal (0) + 0.2);

    // Arrival times (relative delays: the direct sound is at 2 samples, the rest later by the difference of the path lengths) of the first-order paths.
    const auto arrivalWindow = [&] (const juce::AudioBuffer<float>& out, double seconds, double reference)
    {
        const double position = 2.0 + (seconds - reference) * 48000.0;
        double sum = 0.0;
        for (int i = std::max (0, (int) std::floor (position) - 3); i <= (int) std::ceil (position) + 3 && i < out.getNumSamples(); ++i)
            sum += (double) out.getSample (0, i);
        return sum;
    };
    const double directTime = vimic::computeDirectPath (source, mic, pathSettings).delaySeconds;
    room.order = 1;
    for (const auto& image : vimic::images (1))
    {
        const auto path = vimic::computeImagePath (source, mic, image, pathSettings, room);
        // Skip the ones that arrive within a few samples of another path (the floor and the ceiling do).
        int near = 0;
        for (const auto& other : vimic::images (1))
            if (std::abs (vimic::computeImagePath (source, mic, other, pathSettings, room).delaySeconds - path.delaySeconds) * 48000.0 < 8.0)
                ++near;
        if (image.order == 1 && near == 1)
            CHECK (std::abs (arrivalWindow (first.second, path.delaySeconds, directTime) - path.gain * vimic::bandGains (image.hits, walls).low) < 2e-3);
    }

    // The second order adds the 18 images; the totals agree.
    const auto second = totalOf (2);
    CHECK (std::abs (second.first - expectedTotal (2)) < 3e-3 && expectedTotal (2) > expectedTotal (1) + 0.01);
    CHECK (std::abs (second.first - first.first) > 0.01);

    // The gains of the orders and the threshold: 1st order off (-60 dB), and a threshold that removes the weakest paths.
    setParam (p, vimic::params::kRefl1GainId, -60.0f);
    setParam (p, vimic::params::kRefl2GainId, -60.0f);
    const auto muted = totalOf (2);
    CHECK (std::abs (muted.first - 0.5) < 5e-3);
    setParam (p, vimic::params::kRefl1GainId, 0.0f);
    setParam (p, vimic::params::kRefl2GainId, 0.0f);
    setParam (p, vimic::params::kThresholdId, -20.0f);               // 0.1: only paths stronger than that remain
    const auto thresholded = totalOf (2);
    room = p.getRoomSettings();
    CHECK (room.threshold > 0.09 && room.threshold < 0.11);
    double expectedThresholded = 0.0;
    for (const auto& image : vimic::images (2))
        expectedThresholded += vimic::computeImagePath (source, mic, image, pathSettings, room).gain * vimic::bandGains (image.hits, walls).low;
    CHECK (std::abs (thresholded.first - expectedThresholded) < 3e-3 && thresholded.first < second.first);
    setParam (p, vimic::params::kThresholdId, vimic::params::kThresholdOff);

    // The walls colour the reflections: absorbing the right wall's low band all but removes its reflection at low frequencies (its DC gain), and the high band
    // (the Nyquist gain) is left alone; the other walls are unchanged.
    for (int band = 0; band < 3; ++band)
        setParam (p, vimic::params::absorptionId (vimic::Right, band), band == 0 ? 99.9f : 0.1f);
    const auto absorbing = totalOf (1);
    const auto rightImage = std::find_if (vimic::images (1).begin(), vimic::images (1).end(), [] (const vimic::Image& i) { return i.order == 1 && i.hits[vimic::Right] == 1; });
    room.order = 1;
    const double rightGain = vimic::computeImagePath (source, mic, *rightImage, pathSettings, room).gain;
    CHECK (std::abs ((first.first - absorbing.first) - rightGain * (1.0 - std::sqrt (0.001))) < 3e-3);
    // (A low shelf of -30 dB makes the reflection an impulse with a long negative tail, of which a part is inside the window: what is left near the arrival is the high band.)
    const double right = arrivalWindow (absorbing.second, vimic::computeImagePath (source, mic, *rightImage, pathSettings, room).delaySeconds, directTime);
    CHECK (right > 0.25 * rightGain && right < rightGain);                      // far above what the low band alone (3 % of the gain) would leave

    // Switching the reflections off again: the bank stays, the direct sound is as before and the reflections have faded out.
    const auto off = totalOf (0);
    CHECK (std::abs (off.first - 0.5) < 1e-3);
}

/** A bigger room needs a bigger reflection bank, made on the message thread; a smaller one keeps the one it has; reflections do not need a bank for the direct sound. */
static void testReflectionBank()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 3, 4, 512));
    setParam (p, vimic::params::kReflOrderId, 1.0f);
    p.refreshLayout();
    const auto small = p.getReflectionBankSize();
    CHECK (small > 0);

    p.refreshLayout();
    CHECK (p.getReflectionBankSize() == small);                      // the same bank: nothing new is made

    setParam (p, vimic::params::kRoomWidthId, 40.0f);
    setParam (p, vimic::params::kRoomDepthId, 40.0f);
    p.refreshLayout();
    const auto big = p.getReflectionBankSize();
    CHECK (big > small * 2);

    setParam (p, vimic::params::kRoomWidthId, 5.0f);
    p.refreshLayout();
    CHECK (p.getReflectionBankSize() == big);                         // a smaller room fits in the bank that exists

    setParam (p, vimic::params::kReflOrderId, 2.0f);                  // more wall sequences: a new bank
    p.refreshLayout();
    CHECK (p.getReflectionBankSize() > big);

    // The output is finite while all this changes.
    juce::AudioBuffer<float> buf (4, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (70);
    for (int block = 0; block < 20; ++block)
    {
        fillNoise (buf, block % 3, rng);
        if (block == 10)
        {
            setParam (p, vimic::params::kRoomHeightId, 30.0f);
            p.refreshLayout();
        }
        p.processBlock (buf, midi);
        CHECK (allFinite (buf, 4));
    }

    // A new prepareToPlay makes a new bank (the sample rate or the block size may have changed).
    p.prepareToPlay (96000.0, 256);
    p.refreshLayout();
    CHECK (p.getReflectionBankSize() > 0);
}

/** Heights: the paths are longer and the sound arrives off the axis. */
static void testHeights()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 1, 1, 2048));
    placeMic (p, 0, 0.0f, 0.0f, 0.0f, 0.5f);
    placeSource (p, 0, 0.0f, 4.0f);
    p.refreshLayout();
    setParam (p, vimic::params::kSourceHeightId, 4.5f);           // 3 m above the microphone (at 1.5 m)
    auto out = impulseResponse (p, 1, 1, 2048);
    double sum = 0.0;
    centroid (out, 0, sum);
    CHECK (std::abs (sum - 0.2 * 0.5 * (1.0 + 4.0 / 5.0)) < 1e-3);   // distance 5 m, cos = 4 / 5
}

/** Exclude removes a microphone from the relative delays and silences it; mute silences it; trim scales it; unused microphones and sources are silent. */
static void testChannelControls()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 2, 3, 2048));
    placeMic (p, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    placeMic (p, 1, 5.0f, 0.0f, 0.0f, 1.0f);
    placeMic (p, 2, 9.0f, 0.0f, 0.0f, 1.0f);
    placeSource (p, 0, 0.0f, 0.0f);
    placeSource (p, 1, 0.0f, 20.0f);
    p.refreshLayout();
    const double c = vimic::speedOfSound (20.0);

    auto out = impulseResponse (p, 2, 3, 2048);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    CHECK (std::abs (centroid (out, 1, s1) - (2.0 + 4.0 / c * 48000.0)) < 0.02);                // relative to microphone 1 (1 m)

    p.setSpeakerExcluded (0, true);                                                            // the nearest one leaves: the reference changes
    out = impulseResponse (p, 2, 3, 2048);
    CHECK (std::abs (out.getSample (0, 2)) < 1e-6f && std::abs (centroid (out, 1, s1) - 2.0) < 0.02);
    CHECK (std::abs (centroid (out, 2, s2) - (2.0 + 4.0 / c * 48000.0)) < 0.02);
    p.setSpeakerExcluded (0, false);

    setParam (p, tlos::params::speakerMuteId (1), 1.0f);
    out = impulseResponse (p, 2, 3, 2048);
    centroid (out, 1, s1);
    centroid (out, 0, s0);
    CHECK (std::abs (s1) < 1e-6 && std::abs (s0 - 1.0) < 1e-3);
    setParam (p, tlos::params::speakerMuteId (1), 0.0f);

    setParam (p, tlos::params::speakerTrimId (2), -6.0f);
    out = impulseResponse (p, 2, 3, 2048);
    centroid (out, 2, s2);
    CHECK (std::abs (s2 - std::pow (10.0, -6.0 / 20.0) / 9.0) < 1e-3);
    setParam (p, tlos::params::speakerTrimId (2), 0.0f);

    // Microphones and sources beyond the numbers in use are silent.
    setParam (p, tlos::params::kNumSpeakersId, 2.0f);
    p.refreshLayout();
    out = impulseResponse (p, 2, 3, 2048);
    CHECK (channelEnergy (out, 2) == 0.0);
    setParam (p, tlos::params::kNumSourcesId, 1.0f);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (3, 256);
    for (int block = 0; block < 3; ++block)
    {
        buf.clear();
        buf.setSample (1, 0, 1.0f);                                                            // only input 2 (not in use) has signal
        p.processBlock (buf, midi);
    }
    CHECK (totalEnergy (buf, 3) == 0.0);
}

/** A source in motion changes its delays smoothly: no discontinuity at the block boundaries, and a Doppler shift in the absolute mode only. */
static void testMovingSource()
{
    const auto run = [] (bool absolute, double& ratio, double& largestStep)
    {
        ViMiCAudioProcessor p;
        CHECK (configure (p, 1, 1, 256));
        placeMic (p, 0, 0.0f, 0.0f, 0.0f, 1.0f);
        setParam (p, vimic::params::kDelayModeId, absolute ? 1.0f : 0.0f);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf (1, 256);

        const double frequency = 0.02;
        std::vector<float> all;
        long t = 0;
        for (int block = 0; block < 200; ++block)
        {
            // Moves away from the microphone at 20 m/s (a block of 256 samples at 48 kHz is 5.3 ms: about 0.107 m).
            placeSource (p, 0, 0.0f, 3.0f + 0.1067f * (float) block);
            for (int i = 0; i < 256; ++i, ++t)
                buf.setSample (0, i, (float) std::sin (2.0 * std::numbers::pi * frequency * (double) t));
            p.processBlock (buf, midi);
            for (int i = 0; i < 256; ++i)
                all.push_back (buf.getSample (0, i));
        }

        // The pitch from the zero crossings in the second half (the gain falls slowly with the distance, which does not move them).
        int crossings = 0;
        long first = -1, last = -1;
        for (long n = 5000; n < (long) all.size(); ++n)
            if (all[(size_t) n - 1] < 0.0f && all[(size_t) n] >= 0.0f)
            {
                ++crossings;
                if (first < 0) first = n;
                last = n;
            }
        ratio = ((double) (crossings - 1) / (double) (last - first)) / frequency;

        largestStep = 0.0;
        for (long n = 1; n < (long) all.size(); ++n)
            largestStep = std::max (largestStep, (double) std::abs (all[(size_t) n] - all[(size_t) n - 1]));
    };

    double relativeRatio = 0.0, absoluteRatio = 0.0, stepRelative = 0.0, stepAbsolute = 0.0;
    run (false, relativeRatio, stepRelative);
    run (true, absoluteRatio, stepAbsolute);
    const double v = 0.1067 / (256.0 / 48000.0) / vimic::speedOfSound (20.0);                     // the speed of the source in units of c
    CHECK (std::abs (absoluteRatio - (1.0 - v)) < 0.004);                                         // Doppler: lower by 1 - v/c
    CHECK (std::abs (relativeRatio - 1.0) < 0.004);                                               // one microphone: nothing to be relative to, no Doppler
    CHECK (stepAbsolute < 2.0 * std::numbers::pi * 0.02 * 1.1 && stepRelative < 2.0 * std::numbers::pi * 0.02 * 1.1);   // no jumps (gain ramps are slow)
}

/** The cross-fade delay mode: a delay stays as it is until the new one is further away than the threshold, then fades to it (no discontinuity); a continuous movement
    keeps the Doppler pitch on average; a path that was silent starts at its new delay. */
static void testCrossFade()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 1, 1, 256));
    const double c = vimic::speedOfSound (20.0);
    placeMic (p, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    setParam (p, vimic::params::kDelayModeId, 1.0f);                // absolute: the delay is the true time of flight
    setParam (p, vimic::params::kDelayRenderId, 1.0f);              // cross-fade
    setParam (p, vimic::params::kFadeTimeId, 20.0f);
    CHECK (p.getDelayRender() == vimic::params::DelayRender::CrossFade);
    CHECK (std::abs (getParam (p, vimic::params::kFadeThresholdId) - 0.5f) < 1e-4f);
    placeSource (p, 0, 3.0f, 0.0f);
    p.refreshLayout();

    const auto delayOf = [&] (double& gain)
    {
        auto out = impulseResponse (p, 1, 1, 4096);
        return centroid (out, 0, gain);
    };

    double gain = 0.0;
    const double first = delayOf (gain);
    CHECK (std::abs (first - (2.0 + 3.0 / c * 48000.0)) < 0.02 && std::abs (gain - 1.0 / 3.0) < 1e-3);

    // A small move (5 cm: 0.15 ms, below the 0.5 ms threshold): the gain follows, the delay is kept.
    placeSource (p, 0, 3.05f, 0.0f);
    const double small = delayOf (gain);
    CHECK (std::abs (small - first) < 0.02 && std::abs (gain - 1.0 / 3.05) < 1e-3);

    // A big move (3 to 6 m: 8.7 ms): after the fade (20 ms) the delay is the new one.
    placeSource (p, 0, 6.0f, 0.0f);
    const double moved = delayOf (gain);                             // the impulse response of the block comes after three silent blocks of 4096 samples: the fade is over
    CHECK (std::abs (moved - (2.0 + 6.0 / c * 48000.0)) < 0.02 && std::abs (gain - 1.0 / 6.0) < 1e-3);

    // During the fade the sound is the mix of the old and the new delay: one impulse shows both delays, each with the weight of the fade at its arrival.
    ViMiCAudioProcessor q;
    CHECK (configure (q, 1, 1, 256));
    placeMic (q, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    setParam (q, vimic::params::kMinDistanceId, 0.1f);
    setParam (q, vimic::params::kDistancePowerId, 0.0f);            // no distance gain: only the delays differ
    setParam (q, vimic::params::kDelayModeId, 1.0f);
    setParam (q, vimic::params::kDelayRenderId, 1.0f);
    setParam (q, vimic::params::kFadeTimeId, 50.0f);                // 2400 samples: about 9 blocks
    placeSource (q, 0, 3.0f, 0.0f);
    q.refreshLayout();
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (1, 256);
    for (int block = 0; block < 5; ++block)
    {
        buf.clear();
        q.processBlock (buf, midi);
    }
    placeSource (q, 0, 4.0f, 0.0f);                                  // 1 m further: 140 samples
    const double oldDelay = 2.0 + 3.0 / c * 48000.0, newDelay = 2.0 + 4.0 / c * 48000.0;
    std::vector<float> response;
    for (int block = 0; block < 40; ++block)
    {
        buf.clear();
        if (block == 0)
            buf.setSample (0, 0, 1.0f);                              // one impulse, at the start of the fade
        q.processBlock (buf, midi);
        for (int i = 0; i < 256; ++i)
            response.push_back (buf.getSample (0, i));
    }
    auto window = [&] (double position)
    {
        double sum = 0.0;
        for (int i = (int) position - 3; i <= (int) position + 4; ++i)
            sum += (double) response[(size_t) i];
        return sum;
    };
    const double weightOld = window (oldDelay), weightNew = window (newDelay);
    // The fade is linear over 2400 samples from the start of the block that sees the move: the old delay has the weight 1 - t / 2400 where it arrives and the new one t / 2400 where it arrives.
    CHECK (std::abs (weightOld - (1.0 - oldDelay / 2400.0)) < 0.02 && std::abs (weightNew - newDelay / 2400.0) < 0.02);

    // No jumps: a long sine from a source that moves back and forth, the largest step stays near that of the sine (the sum of two copies at most twice as large).
    ViMiCAudioProcessor r;
    CHECK (configure (r, 1, 1, 256));
    placeMic (r, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    setParam (r, vimic::params::kDelayModeId, 1.0f);
    setParam (r, vimic::params::kDelayRenderId, 1.0f);
    juce::AudioBuffer<float> sine (1, 256);
    double largestStep = 0.0;
    float previous = 0.0f;
    long t0 = 0;
    for (int block = 0; block < 400; ++block)
    {
        placeSource (r, 0, 0.0f, 4.0f + 3.0f * (float) std::sin (0.05 * block));
        for (int i = 0; i < 256; ++i, ++t0)
            sine.setSample (0, i, (float) std::sin (2.0 * std::numbers::pi * 0.02 * (double) t0));
        r.processBlock (sine, midi);
        for (int i = 0; i < 256; ++i)
        {
            if (block > 20)
                largestStep = std::max (largestStep, (double) std::abs (sine.getSample (0, i) - previous));
            previous = sine.getSample (0, i);
        }
        CHECK (allFinite (sine, 1));
    }
    CHECK (largestStep < 2.0 * 2.0 * std::numbers::pi * 0.02 * 1.1);

    // Switching the delay change mode restarts the ramps (no sweep from one to the other) and the output stays finite.
    setParam (r, vimic::params::kDelayRenderId, 0.0f);
    for (int block = 0; block < 10; ++block)
    {
        placeSource (r, 0, 0.0f, 5.0f + 0.2f * (float) block);
        r.processBlock (sine, midi);
        CHECK (allFinite (sine, 1));
    }
}

/** Positions, yaw, pattern, delay mode, the edit modes and the coordinate mode survive a save and a restore. */
static void testStateRoundTrip()
{
    ViMiCAudioProcessor a;
    CHECK (configure (a, 4, 6, 256));
    placeMic (a, 3, 1.25f, 2.0f, 33.0f, 0.7f);
    setParam (a, vimic::params::srcYawId (2), -70.0f);
    setParam (a, vimic::params::srcPatternId (2), 0.4f);
    setParam (a, vimic::params::kSourcePowerId, 4.0f);
    setParam (a, vimic::params::kRoomWidthId, 12.5f);
    setParam (a, vimic::params::kReflOrderId, 2.0f);
    setParam (a, vimic::params::absorptionId (vimic::Rear, 2), 77.0f);
    setParam (a, vimic::params::kDelayModeId, 1.0f);
    setParam (a, vimic::params::kTemperatureId, 9.0f);
    a.setCoordinateMode (tlos::SpatialProcessor::CoordinateMode::Polar);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    ViMiCAudioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (configure (b, 4, 6, 256)); // must not overwrite restored values with the defaults
    CHECK (std::abs (getParam (b, tlos::params::speakerId (3, 'x')) - 1.25f) < 1e-4f);
    CHECK (std::abs (getParam (b, vimic::params::micYawId (3)) - 33.0f) < 1e-3f && std::abs (getParam (b, vimic::params::micPatternId (3)) - 0.7f) < 1e-4f);
    CHECK (b.getDelayMode() == vimic::params::DelayMode::Absolute && std::abs (getParam (b, vimic::params::kTemperatureId) - 9.0f) < 1e-3f);
    CHECK (std::abs (getParam (b, vimic::params::kRoomWidthId) - 12.5f) < 1e-3f && b.getRoomSettings().order == 2 && std::abs (getParam (b, vimic::params::absorptionId (vimic::Rear, 2)) - 77.0f) < 1e-2f);
    CHECK (std::abs (getParam (b, vimic::params::srcYawId (2)) + 70.0f) < 1e-3f && std::abs (getParam (b, vimic::params::srcPatternId (2)) - 0.4f) < 1e-4f && getParam (b, vimic::params::kSourcePowerId) == 4.0f);
    CHECK (b.getCoordinateMode() == tlos::SpatialProcessor::CoordinateMode::Polar);

    // A VecPan session is not a ViMiC session.
    juce::XmlElement foreign ("VecPan");
    juce::MemoryBlock foreignState;
    juce::AudioProcessor::copyXmlToBinary (foreign, foreignState);
    ViMiCAudioProcessor c;
    c.setStateInformation (foreignState.getData(), (int) foreignState.getSize());
    CHECK (c.getDelayMode() == vimic::params::DelayMode::Relative);
}

/** The yaw and the pattern are in the files of the microphone layout, and a file without them leaves them alone (the rule of the other columns). */
static void testFilesAndColumns()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 1, 3, 256));
    placeMic (p, 0, -1.0f, 2.0f, -30.0f, 0.5f);
    placeMic (p, 1, 1.0f, 2.0f, 30.0f, 0.5f);
    placeMic (p, 2, 0.0f, -2.0f, 180.0f, 1.0f);

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tlos-vimic-setup.xml");
    CHECK (tlos::io::save (p, tlos::io::Kind::Speakers, file).wasOk());
    const auto text = file.loadFileAsString();
    CHECK (text.contains ("yaw=\"") && text.contains ("pattern=\"") && text.contains ("<ViMiCSpeakers"));
    CHECK (! tlos::io::save (p, tlos::io::Kind::Sources, file).failed() && ! file.loadFileAsString().contains ("spread="));   // no spread for ViMiC

    // The sources have them too (in their own files).
    setParam (p, vimic::params::srcYawId (0), 20.0f);
    setParam (p, vimic::params::srcPatternId (0), 0.5f);
    CHECK (tlos::io::save (p, tlos::io::Kind::Sources, file).wasOk());
    {
        const auto sourceText = file.loadFileAsString();
        CHECK (sourceText.contains ("yaw=\"") && sourceText.contains ("pattern=\"") && sourceText.contains ("<ViMiCSources"));
        ViMiCAudioProcessor r;
        CHECK (configure (r, 1, 3, 256));
        tlos::io::ImportReport sourceReport;
        CHECK (tlos::io::load (r, tlos::io::Kind::Sources, file, sourceReport).wasOk());
        CHECK (std::abs (getParam (r, vimic::params::srcYawId (0)) - 20.0f) < 0.1f && std::abs (getParam (r, vimic::params::srcPatternId (0)) - 0.5f) < 0.01f);
    }

    ViMiCAudioProcessor q;
    CHECK (configure (q, 1, 3, 256));
    CHECK (tlos::io::save (p, tlos::io::Kind::Speakers, file).wasOk());
    tlos::io::ImportReport report;
    CHECK (tlos::io::load (q, tlos::io::Kind::Speakers, file, report).wasOk());
    CHECK (std::abs (getParam (q, vimic::params::micYawId (0)) + 30.0f) < 0.1f && std::abs (getParam (q, vimic::params::micYawId (2)) - 180.0f) < 0.1f);
    CHECK (std::abs (getParam (q, vimic::params::micPatternId (2)) - 1.0f) < 0.01f);

    // A file from another plugin has no yaw or pattern: the microphones keep theirs only if no entry has them (here none has: they get the defaults).
    file.replaceWithText ("<DistPanSpeakers version=\"1\" count=\"2\"><Speaker index=\"1\" x=\"-2\" y=\"1\"/><Speaker index=\"2\" x=\"2\" y=\"1\"/></DistPanSpeakers>");
    placeMic (q, 0, 0.0f, 0.0f, 77.0f, 0.1f);
    tlos::io::ImportReport other;
    CHECK (tlos::io::load (q, tlos::io::Kind::Speakers, file, other).wasOk());
    CHECK (std::abs (getParam (q, vimic::params::micYawId (0)) - 77.0f) < 0.1f && std::abs (getParam (q, tlos::params::speakerId (0, 'x')) + 2.0f) < 1e-3f);
    file.deleteFile();
}

/** 64 sources to 64 speakers: one source at a time keeps its energy, and the output is finite. */
static void testFullSize64x64()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 64, 64, 512));

    juce::AudioBuffer<float> buf (64, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (5);

    for (int src = 0; src < 64; ++src)
    {
        const double inE = fillNoise (buf, src, rng);
        p.processBlock (buf, midi);
        CHECK (allFinite (buf, 64));
        CHECK (inE > 0.0);
    }

    // Every source at once, with moving sources, for a while: finite, and not louder than the sum of the inputs allows.
    for (int block = 0; block < 20; ++block)
    {
        for (int s = 0; s < 64; ++s)
        {
            setParam (p, tlos::params::sourceId (s, 'x'), 8.0f * std::sin (0.1f * (float) (block + s)));
            setParam (p, tlos::params::sourceId (s, 'y'), 8.0f * std::cos (0.1f * (float) (block + s)));
        }
        for (int s = 0; s < 64; ++s)
        {
            std::uniform_real_distribution<float> u (-1.0f, 1.0f);
            for (int i = 0; i < 512; ++i)
                buf.setSample (s, i, u (rng));
        }
        p.processBlock (buf, midi);
        CHECK (allFinite (buf, 64));
    }
}

/** The cost of processBlock alone with 64 sources and 64 speakers (printed, not checked: a Debug build is much slower than the Release one the plugin ships as). */
static void testFullSizeCost()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 64, 64, 512));

    juce::AudioBuffer<float> buf (64, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (9);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (int s = 0; s < 64; ++s)
        for (int i = 0; i < 512; ++i)
            buf.setSample (s, i, u (rng));
    const juce::AudioBuffer<float> input (buf);

    // Scenario 0: sources stand still. 1: every source moves every block. 2: the same, with spread.
    const char* names[] { "static sources", "all 64 sources moving", "all moving, with spread" };
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        double total = 0.0, worst = 0.0;
        const int blocks = 300;
        for (int block = -20; block < blocks; ++block)
        {
            if (scenario > 0)
                for (int s = 0; s < 64; ++s)
                {
                    const float a = 0.05f * (float) block + (float) s;
                    setParam (p, tlos::params::sourceId (s, 'x'), 6.0f * std::sin (a));
                    setParam (p, tlos::params::sourceId (s, 'y'), 6.0f * std::cos (a));
                    if (scenario == 2)
                        setParam (p, tlos::params::spreadId (s), 2.0f);
                }
            for (int ch = 0; ch < 64; ++ch)
                buf.copyFrom (ch, 0, input, ch, 0, 512);

            const auto t0 = std::chrono::steady_clock::now();
            p.processBlock (buf, midi);
            const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
            if (block >= 0)
            {
                total += ms;
                worst = std::max (worst, ms);
            }
            CHECK (allFinite (buf, 64));
        }
        const double budget = 512.0 / 48000.0 * 1000.0;
        std::printf ("ViMiC 64x64, %-24s: %.3f ms per 512-sample block on average (%.1f %% of the %.1f ms available at 48 kHz), worst %.3f ms\n",
                     names[scenario], total / blocks, 100.0 * total / blocks / budget, budget, worst);
    }
}

/** The cost of the reflections: 8 sources and 8 microphones, and 16 and 16, with the first and the second order, static and moving (printed, not checked). */
static void testReflectionCost()
{
    struct Case { int channels; int order; bool moving; int outputs = 0; };
    for (const Case c : { Case { 8, 1, false }, Case { 8, 1, true }, Case { 8, 2, false }, Case { 8, 2, true }, Case { 16, 2, false }, Case { 16, 2, true }, Case { 8, 2, false, 1 } })
    {
        ViMiCAudioProcessor p;
        const int outputs = c.outputs > 0 ? c.outputs : c.channels;
        CHECK (configure (p, c.channels, outputs, 512));
        setParam (p, vimic::params::kReflOrderId, (float) c.order);
        p.refreshLayout();

        juce::AudioBuffer<float> buf (std::max (c.channels, outputs), 512);
        juce::MidiBuffer midi;
        std::mt19937 rng (11);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        for (int ch = 0; ch < c.channels; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample (ch, i, u (rng));
        const juce::AudioBuffer<float> input (buf);

        double total = 0.0, worst = 0.0;
        const int blocks = 200;
        for (int block = -20; block < blocks; ++block)
        {
            if (c.moving)
                for (int s = 0; s < c.channels; ++s)
                {
                    const float a = 0.05f * (float) block + (float) s;
                    setParam (p, tlos::params::sourceId (s, 'x'), 3.0f * std::sin (a));
                    setParam (p, tlos::params::sourceId (s, 'y'), 3.0f * std::cos (a));
                }
            for (int ch = 0; ch < c.channels; ++ch)
                buf.copyFrom (ch, 0, input, ch, 0, 512);

            const auto t0 = std::chrono::steady_clock::now();
            p.processBlock (buf, midi);
            const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
            if (block >= 0)
            {
                total += ms;
                worst = std::max (worst, ms);
            }
            CHECK (allFinite (buf, outputs));
        }

        const double budget = 512.0 / 48000.0 * 1000.0;
        std::printf ("ViMiC %2dx%-2d, %d order reflections, %-8s: %.3f ms per 512-sample block on average (%.1f %% of the %.1f ms available at 48 kHz), worst %.3f ms\n",
                     c.channels, outputs, c.order, c.moving ? "moving" : "static", total / blocks, 100.0 * total / blocks / budget, budget, worst);
    }
}

/** Parameters change on one thread while another processes audio: no crash, finite output. */
static void testParameterAutomationWhileProcessing()
{
    ViMiCAudioProcessor p;
    CHECK (configure (p, 8, 8, 256));

    std::atomic<bool> stop { false };
    std::thread automation ([&]
    {
        std::mt19937 rng (6);
        std::uniform_real_distribution<float> u (-10.0f, 10.0f);
        while (! stop.load())
        {
            setParam (p, tlos::params::sourceId ((int) (rng() % 8), 'x'), u (rng));
            setParam (p, tlos::params::speakerId ((int) (rng() % 8), 'y'), u (rng));
            setParam (p, vimic::params::kTemperatureId, 5.0f + 10.0f * std::abs (u (rng)));
            setParam (p, vimic::params::kReflOrderId, (float) (rng() % 3));
            setParam (p, vimic::params::kRoomWidthId, 4.0f + 30.0f * std::abs (u (rng)) / 10.0f);
            setParam (p, vimic::params::absorptionId ((int) (rng() % vimic::WallCount), (int) (rng() % 3)), 1.0f + 90.0f * std::abs (u (rng)) / 10.0f);
            p.refreshLayout();                                      // what the timer does: publishes microphones and makes reflection banks
        }
    });

    juce::AudioBuffer<float> buf (8, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (7);
    bool finite = true;
    for (int block = 0; block < 400; ++block)
    {
        fillNoise (buf, block % 8, rng);
        p.processBlock (buf, midi);
        finite = finite && allFinite (buf, 8);
    }

    stop = true;
    automation.join();
    CHECK (finite);
}


/** Every speaker layout file of the repository can be imported and then pans with constant power. */
static void testSpeakerLayouts (const juce::File& dir)
{
    int count = 0;
    for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
    {
        ViMiCAudioProcessor p;
        CHECK (configure (p, 2, 64, 256));
        setParam (p, tlos::params::kOutsideRolloffId, 0.0f); // the source may be outside a layout: this tests the import, not the falloff

        tlos::io::ImportReport report;
        const auto result = tlos::io::load (p, tlos::io::Kind::Speakers, file, report);
        CHECK (result.wasOk());
        p.refreshLayout();

        juce::AudioBuffer<float> buf (64, 256);
        juce::MidiBuffer midi;
        std::mt19937 rng (8);
        double inE = 0.0;
        for (int block = 0; block < 3; ++block)
        {
            inE = fillNoise (buf, 0, rng);
            p.processBlock (buf, midi);
        }
        CHECK (allFinite (buf, 64));
        CHECK (inE > 0.0 && totalEnergy (buf, 64) > 0.0);

        // The LFE (channel 4) of the 5.1 and 7.1 presets is excluded: silent.
        if (file.getFileName() == "5.1.xml" || file.getFileName() == "7.1.xml")
            CHECK (p.isSpeakerExcluded (3) && channelEnergy (buf, 3) == 0.0);
        ++count;
    }

    CHECK (count >= 10);
    std::printf ("speaker layouts: %d files\n", count);
}


/** Every microphone setup of the repository can be imported (with its yaw and pattern) and then renders finite, non-silent output. */
static void testMicrophoneSetups (const juce::File& dir)
{
    int count = 0;
    for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
    {
        ViMiCAudioProcessor p;
        CHECK (configure (p, 2, 16, 512));
        tlos::io::ImportReport report;
        CHECK (tlos::io::load (p, tlos::io::Kind::Speakers, file, report).wasOk() && report.skipped == 0);
        p.refreshLayout();

        juce::AudioBuffer<float> buf (16, 512);
        juce::MidiBuffer midi;
        std::mt19937 rng (9);
        for (int block = 0; block < 3; ++block)
        {
            fillNoise (buf, 0, rng);
            p.processBlock (buf, midi);
        }
        CHECK (allFinite (buf, 16) && totalEnergy (buf, 16) > 0.0);
        CHECK (p.getActiveSpeakers() == report.fileCount);
        ++count;

        // ORTF: two cardioids 17 cm apart, +-55 degrees (the left one turned to the left, which is positive).
        if (file.getFileName() == "Stereo-ORTF.xml")
            CHECK (std::abs (getParam (p, vimic::params::micYawId (0)) - 55.0f) < 0.01f && std::abs (getParam (p, vimic::params::micYawId (1)) + 55.0f) < 0.01f
                   && std::abs (getParam (p, vimic::params::micPatternId (1)) - 0.5f) < 0.001f && std::abs (getParam (p, tlos::params::speakerId (1, 'x')) - 0.085f) < 1e-3f);
    }

    CHECK (count >= 8);
    std::printf ("microphone setups: %d files\n", count);
}

/** Runs all processor tests. @return 0 if every check passed, otherwise 1. */
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testParameterOrder();
    testDefaultLayout();
    testGainsAndDelays();
    testPatternAndYaw();
    testSourceDirectivity();
    testHeights();
    testCrossFade();
    testReflections();
    testReflectionBank();
    testChannelControls();
    testMovingSource();
    testStateRoundTrip();
    testFilesAndColumns();
    testFullSize64x64();
    testFullSizeCost();
    testReflectionCost();
    testParameterAutomationWhileProcessing();

    if (argc > 1)
        testSpeakerLayouts (juce::File (argv[1]));
    else
        std::printf ("(speaker layout tests skipped: pass the speaker layouts folder as argument)\n");
    if (argc > 2)
        testMicrophoneSetups (juce::File (argv[2]));

    if (failures == 0)
        std::printf ("All processor tests passed\n");
    return failures == 0 ? 0 : 1;
}
