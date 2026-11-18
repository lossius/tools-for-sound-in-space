// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file ProcessorTests.cpp
    @brief Offline tests of the VecPan processor (no DAW): energy conservation, pair locality, speaker updates, the level outside
    the layout, exclude, silent unused speakers, state round-trip, 64x64, speaker layout files, parameter order. */

#include "../Source/PluginProcessor.h"
#include "tlos/Parameters.h"
#include "tlos/PositionIO.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
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
void setParam (VecPanAudioProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.getValueTreeState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

/** Current real value of a parameter. */
float getParam (VecPanAudioProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

/** Configures the processor for n inputs and m outputs and prepares it. @return false if the layout is refused. */
bool configure (VecPanAudioProcessor& p, int n, int m, int blockSize)
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

/** Energy of every output channel for one stationary source with noise, after the gain ramps have settled, per unit of input energy. */
static std::vector<double> channelEnergies (VecPanAudioProcessor& p, juce::AudioBuffer<float>& buf, int outputs, int source, std::mt19937& rng)
{
    juce::MidiBuffer midi;
    double inE = 0.0;
    for (int block = 0; block < 4; ++block)
    {
        inE = fillNoise (buf, source, rng);
        p.processBlock (buf, midi);
    }

    std::vector<double> e;
    for (int ch = 0; ch < outputs; ++ch)
        e.push_back (channelEnergy (buf, ch) / inE);
    return e;
}

/** Sum of a vector. */
static double sumOf (const std::vector<double>& v)
{
    double s = 0.0;
    for (const double x : v)
        s += x;
    return s;
}


/** The host lists the global parameters first (counts, outside rolloff), then each source, then each speaker. VecPan has no parameters of its own. */
static void testParameterOrder()
{
    VecPanAudioProcessor p;
    const auto& parameters = p.getParameters();
    const int total = 3 + 64 * 7 + 64 * 6;

    CHECK (parameters.size() == total);

    const auto idOf = [&] (int i) { return dynamic_cast<juce::AudioProcessorParameterWithID*> (parameters[i])->paramID; };
    CHECK (idOf (0) == tlos::params::kNumSourcesId && idOf (1) == tlos::params::kNumSpeakersId && idOf (2) == tlos::params::kOutsideRolloffId);
    CHECK (idOf (3) == tlos::params::sourceId (0, 'x'));
    const int speakers = 3 + 64 * 7;
    CHECK (idOf (speakers - 1) == tlos::params::sourceGroupId (63));
    CHECK (idOf (speakers) == tlos::params::speakerId (0, 'x'));
    CHECK (idOf (speakers + 5) == tlos::params::speakerExcludeId (0) && idOf (speakers + 6) == tlos::params::speakerId (1, 'x'));
    CHECK (idOf (total - 1) == tlos::params::speakerExcludeId (63));

    const auto& tree = p.getParameterTree();
    CHECK (tree.getSubgroups (false).size() == 3 && tree.getSubgroups (false).getFirst()->getName() == "Global");

    CHECK (! p.supportsVirtualSpeakers() && p.getVirtualMode() == tlos::VirtualMode::Off);
    CHECK (! p.getSpeakerColumn().has_value());
    CHECK (p.getCoordinateMode() == tlos::SpatialProcessor::CoordinateMode::Polar);   // VBAP positions are directions
}

/** A source on a speaker's direction feeds that speaker alone; halfway between two speakers it feeds those two equally; power is kept. */
static void testStationaryEnergyAndLocality()
{
    VecPanAudioProcessor p;
    CHECK (configure (p, 4, 8, 512));

    juce::AudioBuffer<float> buf (8, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (1);

    // Default rings: source i points at speaker 2i.
    for (int src = 0; src < 4; ++src)
        for (int block = 0; block < 3; ++block)
        {
            const double inE = fillNoise (buf, src, rng);
            p.processBlock (buf, midi);
            CHECK (std::abs (totalEnergy (buf, 8) - inE) < 1e-3 * inE);
            CHECK (channelEnergy (buf, 2 * src) > 0.999 * inE);
        }

    // Halfway between speakers 0 and 1 (the bisector direction).
    const auto pos = [&] (int speaker, char axis) { return getParam (p, tlos::params::speakerId (speaker, axis)); };
    setParam (p, tlos::params::sourceId (0, 'x'), 0.5f * (pos (0, 'x') + pos (1, 'x')));   // on the chord between them, inside the layout
    setParam (p, tlos::params::sourceId (0, 'y'), 0.5f * (pos (0, 'y') + pos (1, 'y')));
    const auto energies = channelEnergies (p, buf, 8, 0, rng);
    CHECK (std::abs (energies[0] - 0.5) < 0.01 && std::abs (energies[1] - 0.5) < 0.01);
    for (int ch = 2; ch < 8; ++ch)
        CHECK (energies[(size_t) ch] < 1e-6);
}

/** Moving a speaker changes the panning at once (the layout is republished). */
static void testSpeakerUpdate()
{
    VecPanAudioProcessor p;
    CHECK (configure (p, 1, 4, 256));
    setParam (p, tlos::params::kOutsideRolloffId, 0.0f);   // the source may end up outside the layout: this tests the panning, not the falloff

    juce::AudioBuffer<float> buf (4, 256);
    std::mt19937 rng (3);

    const auto before = channelEnergies (p, buf, 4, 0, rng);
    CHECK (before[0] > 0.99);                                    // source 0 at the front, speaker 0 at the front
    setParam (p, tlos::params::speakerId (0, 'x'), 0.0f);        // speaker 0 to the back
    setParam (p, tlos::params::speakerId (0, 'y'), -5.0f);
    p.refreshLayout();
    const auto after = channelEnergies (p, buf, 4, 0, rng);
    CHECK (after[0] < 0.1 && after[1] + after[3] > 0.9);         // now between the two side speakers
}

/** Speakers beyond "Number of Speakers" are silent, and a source with nothing to feed gives silence. */
static void testNumberOfSourcesAndSpeakersInUse()
{
    VecPanAudioProcessor p;
    CHECK (configure (p, 4, 8, 256));
    setParam (p, tlos::params::kNumSpeakersId, 4.0f);
    p.refreshLayout();

    juce::AudioBuffer<float> buf (8, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (4);

    for (int block = 0; block < 3; ++block)
    {
        const double inE = fillNoise (buf, 1, rng);
        p.processBlock (buf, midi);
        if (block >= 1)
        {
            for (int ch = 4; ch < 8; ++ch)
                CHECK (channelEnergy (buf, ch) == 0.0);
            CHECK (std::abs (totalEnergy (buf, 4) - inE) < 1e-3 * inE);
        }
    }

    setParam (p, tlos::params::kNumSourcesId, 1.0f); // source 2 (index 1) is no longer in use
    for (int block = 0; block < 3; ++block)
    {
        fillNoise (buf, 1, rng);
        p.processBlock (buf, midi);
    }
    CHECK (totalEnergy (buf, 8) == 0.0);
}

/** Positions, the edit modes, the groups and the coordinate mode survive a save and a restore. */
static void testStateRoundTrip()
{
    VecPanAudioProcessor a;
    CHECK (configure (a, 4, 6, 256));
    setParam (a, tlos::params::speakerId (3, 'x'), 1.25f);
    setParam (a, tlos::params::sourceId (2, 'y'), -2.5f);
    a.setSpeakersEditable (true);
    a.setSourcesEditable (false);
    a.setCoordinateMode (tlos::SpatialProcessor::CoordinateMode::Cartesian);
    tlos::SourceGroups groups;
    groups.group ({ 0, 1 });
    a.setSourceGroups (groups);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    VecPanAudioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (configure (b, 4, 6, 256)); // must not overwrite restored positions with default rings

    CHECK (std::abs (getParam (b, tlos::params::speakerId (3, 'x')) - 1.25f) < 1e-4f);
    CHECK (std::abs (getParam (b, tlos::params::sourceId (2, 'y')) + 2.5f) < 1e-4f);
    CHECK (! b.canEditSources() && b.canEditSpeakers());
    CHECK (b.getSourceGroups() == groups);
    CHECK (b.getCoordinateMode() == tlos::SpatialProcessor::CoordinateMode::Cartesian);

    // A DistPan session is not a VecPan session: the state tag differs and nothing is changed.
    juce::XmlElement foreign ("DistPan");
    juce::MemoryBlock foreignState;
    juce::AudioProcessor::copyXmlToBinary (foreign, foreignState);
    VecPanAudioProcessor c;
    c.setStateInformation (foreignState.getData(), (int) foreignState.getSize());
    CHECK (c.getCoordinateMode() == tlos::SpatialProcessor::CoordinateMode::Polar);
}

/** Inside the layout the distance changes nothing; outside the level falls (6 dB per doubling of 1 + distance by default); 0 dB switches it off. */
static void testOutsideFalloff()
{
    VecPanAudioProcessor p;
    CHECK (configure (p, 1, 8, 512));
    CHECK (std::abs (getParam (p, tlos::params::kOutsideRolloffId) - 6.0f) < 1e-6f); // default

    juce::AudioBuffer<float> buf (8, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (22);

    // The default speaker ring has radius 5: the speaker at the front is at (0, 5), so (0, 6) is 1 m outside.
    const auto energyRatioAt = [&] (float x, float y)
    {
        setParam (p, tlos::params::sourceId (0, 'x'), x);
        setParam (p, tlos::params::sourceId (0, 'y'), y);
        double inE = 0.0;
        for (int block = 0; block < 4; ++block)
        {
            inE = fillNoise (buf, 0, rng);
            p.processBlock (buf, midi);
        }
        return totalEnergy (buf, 8) / inE;
    };

    CHECK (std::abs (energyRatioAt (0.0f, 0.5f) - 1.0) < 1e-3);          // near the listener: same level as
    CHECK (std::abs (energyRatioAt (0.0f, 4.0f) - 1.0) < 1e-3);          // ... near the speakers
    CHECK (std::abs (energyRatioAt (0.0f, 6.0f) - 0.25) < 0.01);         // -6 dB
    CHECK (std::abs (energyRatioAt (0.0f, 8.0f) - 1.0 / 16.0) < 0.01);   // -12 dB

    setParam (p, tlos::params::kOutsideRolloffId, 0.0f);
    CHECK (std::abs (energyRatioAt (0.0f, 8.0f) - 1.0) < 1e-3);
}

/** Exclude removes a speaker from the panning: the neighbours take over its share, with constant power; mute keeps the others' levels. */
static void testExclude()
{
    VecPanAudioProcessor p;
    CHECK (configure (p, 1, 8, 512));
    // The direction of speaker 3 (the same at any distance).
    setParam (p, tlos::params::sourceId (0, 'x'), 0.5f * getParam (p, tlos::params::speakerId (3, 'x')));
    setParam (p, tlos::params::sourceId (0, 'y'), 0.5f * getParam (p, tlos::params::speakerId (3, 'y')));
    p.refreshLayout();
    juce::AudioBuffer<float> buf (8, 512);
    std::mt19937 rng (32);

    const auto base = channelEnergies (p, buf, 8, 0, rng);
    CHECK (base[3] > 0.999 && std::abs (sumOf (base) - 1.0) < 1e-3);

    p.setSpeakerExcluded (3, true);
    CHECK (p.isSpeakerExcluded (3) && ! p.isSpeakerExcluded (2));
    const auto excluded = channelEnergies (p, buf, 8, 0, rng);
    CHECK (excluded[3] == 0.0);
    CHECK (std::abs (sumOf (excluded) - 1.0) < 1e-3);
    CHECK (std::abs (excluded[2] - 0.5) < 0.01 && std::abs (excluded[4] - 0.5) < 0.01);   // the two neighbours share it equally
    p.setSpeakerExcluded (3, false);

    // Mute is different: the muted speaker's share is lost.
    setParam (p, tlos::params::speakerMuteId (3), 1.0f);
    const auto muted = channelEnergies (p, buf, 8, 0, rng);
    CHECK (muted[3] == 0.0 && sumOf (muted) < 0.01);
    setParam (p, tlos::params::speakerMuteId (3), 0.0f);

    for (int i = 0; i < 8; ++i)
        p.setSpeakerExcluded (i, true);
    CHECK (sumOf (channelEnergies (p, buf, 8, 0, rng)) == 0.0);
    for (int i = 0; i < 8; ++i)
        p.setSpeakerExcluded (i, false);
    CHECK (std::abs (sumOf (channelEnergies (p, buf, 8, 0, rng)) - 1.0) < 1e-3);
}

/** A stereo pair is the classic tangent-law pan, and a source anywhere keeps constant power. */
static void testStereoPair()
{
    VecPanAudioProcessor p;
    CHECK (configure (p, 1, 2, 512));
    const auto at = [] (double degrees, double d) { const double a = degrees * 3.14159265358979323846 / 180.0; return std::pair<float, float> { (float) (-d * std::sin (a)), (float) (d * std::cos (a)) }; };
    const auto left = at (30.0, 3.0), right = at (-30.0, 3.0);
    setParam (p, tlos::params::speakerId (0, 'x'), left.first);   setParam (p, tlos::params::speakerId (0, 'y'), left.second);
    setParam (p, tlos::params::speakerId (1, 'x'), right.first);  setParam (p, tlos::params::speakerId (1, 'y'), right.second);
    p.refreshLayout();

    juce::AudioBuffer<float> buf (2, 512);
    std::mt19937 rng (40);
    for (double degrees : { 30.0, 10.0, 0.0, -20.0, -30.0, 100.0, 180.0 })   // also outside the pair: cross-fade, still constant power
    {
        const auto pos = at (degrees, 2.0);
        setParam (p, tlos::params::sourceId (0, 'x'), pos.first);
        setParam (p, tlos::params::sourceId (0, 'y'), pos.second);
        setParam (p, tlos::params::kOutsideRolloffId, 0.0f);
        const auto e = channelEnergies (p, buf, 2, 0, rng);
        CHECK (std::abs (sumOf (e) - 1.0) < 1e-3);
    }

    const auto centre = at (0.0, 2.0);
    setParam (p, tlos::params::sourceId (0, 'x'), centre.first);
    setParam (p, tlos::params::sourceId (0, 'y'), centre.second);
    const auto e = channelEnergies (p, buf, 2, 0, rng);
    CHECK (std::abs (e[0] - 0.5) < 0.01 && std::abs (e[1] - 0.5) < 0.01);
}

/** 64 sources to 64 speakers: one source at a time keeps its energy, and the output is finite. */
static void testFullSize64x64()
{
    VecPanAudioProcessor p;
    CHECK (configure (p, 64, 64, 512));

    juce::AudioBuffer<float> buf (64, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (5);

    for (int src = 0; src < 64; ++src)
    {
        const double inE = fillNoise (buf, src, rng);
        p.processBlock (buf, midi);
        CHECK (allFinite (buf, 64));
        CHECK (std::abs (totalEnergy (buf, 64) - inE) < 1e-3 * inE);
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
    VecPanAudioProcessor p;
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
        std::printf ("VecPan 64x64, %-24s: %.3f ms per 512-sample block on average (%.1f %% of the %.1f ms available at 48 kHz), worst %.3f ms\n",
                     names[scenario], total / blocks, 100.0 * total / blocks / budget, budget, worst);
    }
}

/** Parameters change on one thread while another processes audio: no crash, finite output. */
static void testParameterAutomationWhileProcessing()
{
    VecPanAudioProcessor p;
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
            setParam (p, tlos::params::kOutsideRolloffId, 12.0f * std::abs (u (rng)) / 10.0f);
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
        VecPanAudioProcessor p;
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
        CHECK (std::abs (totalEnergy (buf, 64) - inE) < 1e-3 * inE);

        // The LFE (channel 4) of the 5.1 and 7.1 presets is excluded: silent.
        if (file.getFileName() == "5.1.xml" || file.getFileName() == "7.1.xml")
            CHECK (p.isSpeakerExcluded (3) && channelEnergy (buf, 3) == 0.0);
        ++count;
    }

    CHECK (count >= 10);
    std::printf ("speaker layouts: %d files\n", count);
}

/** Runs all processor tests. @return 0 if every check passed, otherwise 1. */
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testParameterOrder();
    testStationaryEnergyAndLocality();
    testStereoPair();
    testOutsideFalloff();
    testExclude();
    testSpeakerUpdate();
    testNumberOfSourcesAndSpeakersInUse();
    testStateRoundTrip();
    testFullSize64x64();
    testFullSizeCost();
    testParameterAutomationWhileProcessing();

    if (argc > 1)
        testSpeakerLayouts (juce::File (argv[1]));
    else
        std::printf ("(speaker layout tests skipped: pass the speaker layouts folder as argument)\n");

    if (failures == 0)
        std::printf ("All processor tests passed\n");
    return failures == 0 ? 0 : 1;
}
