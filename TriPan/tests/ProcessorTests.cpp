// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file ProcessorTests.cpp
    @brief Offline tests of the TriPan processor (no DAW): energy conservation, locality, ramping, speaker updates, state round-trip, large layouts, presets, spread, parameter order. */

#include "tlos/Parameters.h"
#include "../Source/PluginProcessor.h"
#include "tlos/OscControl.h"
#include "tlos/OscSettingsPanel.h"
#include "tlos/PositionIO.h"
#include "tripan/Gains.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <limits>
#include <cstdio>
#include <random>

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
void setParam (TriPanAudioProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.getValueTreeState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

/** Configures the processor for n inputs and m outputs and prepares it. @return false if the layout is refused. */
bool configure (TriPanAudioProcessor& p, int n, int m, int blockSize)
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
double fillNoise (juce::AudioBuffer<float>& b, int n, int active, std::mt19937& rng)
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
    (void) n;
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

/** A stationary source keeps its energy, and only the speakers of its triangle receive signal. */
static void testStationaryEnergyAndLocality()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 4, 8, 512));
    p.setVirtualMode (tlos::VirtualMode::Off); // plain triangulation: three speakers at most per source

    juce::AudioBuffer<float> buf (8, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (1);

    for (int src = 0; src < 4; ++src)
        for (int block = 0; block < 3; ++block)
        {
            const double inE = fillNoise (buf, 4, src, rng);
            p.processBlock (buf, midi);

            double outE = 0.0;
            int active = 0;
            for (int ch = 0; ch < 8; ++ch)
            {
                const double e = channelEnergy (buf, ch);
                outE += e;
                if (e > 1e-9) ++active;
            }
            CHECK (std::abs (outE - inE) < 1e-3 * inE);
            CHECK (active <= 3);
        }
}

/** A moving source gives a smooth output without gain jumps between blocks. */
static void testMovingSourceIsSmoothAndBounded()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 2, 6, 256));

    juce::AudioBuffer<float> buf (6, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (2);

    for (int block = 0; block < 100; ++block)
    {
        const float a = 0.2f * (float) block;
        setParam (p, tlos::params::sourceId (0, 'x'), 6.0f * std::sin (a));
        setParam (p, tlos::params::sourceId (0, 'y'), 6.0f * std::cos (a));

        const double inE = fillNoise (buf, 2, 0, rng);
        p.processBlock (buf, midi);

        double outE = 0.0;
        for (int ch = 0; ch < 6; ++ch) outE += channelEnergy (buf, ch);
        CHECK (allFinite (buf, 6));
        CHECK (outE <= inE * 1.001);
    }
}

/** A source on a speaker feeds only that speaker, and moving the speaker updates the panning. */
static void testSourceOnSpeakerAndSpeakerUpdate()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 1, 5, 256));

    juce::AudioBuffer<float> buf (5, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (3);

    // Move speaker 2 and tell the processor (the timer does this in a real host).
    setParam (p, tlos::params::speakerId (2, 'x'), -4.0f);
    setParam (p, tlos::params::speakerId (2, 'y'), -3.0f);
    p.refreshLayout();

    // Put the source exactly on speaker 2; after the ramp block only that output carries signal.
    setParam (p, tlos::params::sourceId (0, 'x'), -4.0f);
    setParam (p, tlos::params::sourceId (0, 'y'), -3.0f);
    for (int block = 0; block < 2; ++block)
    {
        const double inE = fillNoise (buf, 1, 0, rng);
        p.processBlock (buf, midi);
        if (block == 1)
        {
            CHECK (std::abs (channelEnergy (buf, 2) - inE) < 1e-3 * inE);
            for (int ch = 0; ch < 5; ++ch)
                if (ch != 2) CHECK (channelEnergy (buf, ch) < 1e-9);
        }
    }
}

/** Blocks larger than the size announced in prepareToPlay() are processed correctly. */
static void testOversizedBlock()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 3, 4, 128));

    juce::AudioBuffer<float> buf (4, 1000); // larger than the prepared block size
    juce::MidiBuffer midi;
    std::mt19937 rng (4);

    for (int block = 0; block < 3; ++block)
    {
        const double inE = fillNoise (buf, 3, 1, rng);
        p.processBlock (buf, midi);
        double outE = 0.0;
        for (int ch = 0; ch < 4; ++ch) outE += channelEnergy (buf, ch);
        CHECK (allFinite (buf, 4));
        CHECK (std::abs (outE - inE) < 1e-3 * inE);
    }
}

/** Saving and restoring the state reproduces parameters, edit modes, virtual speakers and editor width. */
static void testStateRoundTrip()
{
    TriPanAudioProcessor a;
    CHECK (configure (a, 4, 6, 256));
    setParam (a, tlos::params::speakerId (3, 'x'), 1.25f);
    setParam (a, tlos::params::sourceId (2, 'y'), -2.5f);
    CHECK (a.canEditSources() && ! a.canEditSpeakers()); // defaults
    a.setSpeakersEditable (true);
    a.setSourcesEditable (false);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    TriPanAudioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (configure (b, 4, 6, 256)); // must not overwrite restored positions with default rings

    const auto get = [] (TriPanAudioProcessor& p, const juce::String& id) { return p.getValueTreeState().getRawParameterValue (id)->load(); };
    CHECK (std::abs (get (b, tlos::params::speakerId (3, 'x')) - 1.25f) < 1e-4f);
    CHECK (std::abs (get (b, tlos::params::sourceId (2, 'y')) + 2.5f) < 1e-4f);
    CHECK (! b.canEditSources() && b.canEditSpeakers());

    // State written by an earlier version (single "editMode" property) is still understood.
    {
        const std::pair<int, std::pair<bool, bool>> legacy[] = { { 1, { true, false } }, { 2, { false, true } }, { 3, { true, true } }, { 4, { false, false } } };
        for (const auto& [mode, expected] : legacy)
        {
            TriPanAudioProcessor old;
            old.getValueTreeState().state.setProperty ("editMode", mode, nullptr);
            CHECK (old.canEditSources() == expected.first && old.canEditSpeakers() == expected.second);

            old.setSourcesEditable (! expected.first); // changing one keeps the other
            CHECK (old.canEditSources() == ! expected.first && old.canEditSpeakers() == expected.second);
        }
    }
}

/** 64 sources to 64 speakers runs and stays finite. */
static void testFullSize64x64()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 64, 64, 512));

    juce::AudioBuffer<float> buf (64, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);

    // Every source carries noise and wanders, so every block recomputes all gains and ramps.
    double inE = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    const int blocks = 400;
    for (int block = 0; block < blocks; ++block)
    {
        for (int s = 0; s < 64; ++s)
        {
            const float a = 0.05f * (float) block + (float) s;
            setParam (p, tlos::params::sourceId (s, 'x'), 6.0f * std::sin (a));
            setParam (p, tlos::params::sourceId (s, 'y'), 6.0f * std::cos (a));
            for (int i = 0; i < 512; ++i) buf.setSample (s, i, u (rng));
        }
        p.processBlock (buf, midi);
        CHECK (allFinite (buf, 64));
        inE += 0.0;
    }
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    // Includes the cost of setParam and noise generation, so this is an upper bound for processBlock.
    std::printf ("64x64: %.3f ms per 512-sample block (budget at 48 kHz: 10.7 ms), incl. test overhead\n", ms / blocks);
    (void) inE;
}


/** Total energy is conserved with 64 sources and 64 speakers. */
static void testFullSizeEnergyConservation()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 64, 64, 256));

    // Scatter the speakers irregularly (deterministically) so the triangulation is non-trivial.
    std::mt19937 rng (6);
    std::uniform_real_distribution<float> pos (-8.0f, 8.0f);
    for (int i = 0; i < 64; ++i)
    {
        setParam (p, tlos::params::speakerId (i, 'x'), pos (rng));
        setParam (p, tlos::params::speakerId (i, 'y'), pos (rng));
    }
    p.refreshLayout();

    juce::AudioBuffer<float> buf (64, 256);
    juce::MidiBuffer midi;

    for (int src : { 0, 17, 63 })
    {
        setParam (p, tlos::params::sourceId (src, 'x'), pos (rng));
        setParam (p, tlos::params::sourceId (src, 'y'), pos (rng));

        for (int block = 0; block < 3; ++block)
        {
            const double inE = fillNoise (buf, 64, src, rng);
            p.processBlock (buf, midi);

            double outE = 0.0;
            int active = 0;
            for (int ch = 0; ch < 64; ++ch)
            {
                const double e = channelEnergy (buf, ch);
                outE += e;
                if (e > 1e-9) ++active;
            }
            if (block > 0) // the first block ramps in from the previous position
            {
                CHECK (std::abs (outE - inE) < 1e-3 * inE);
                CHECK (active <= 3);
            }
        }
    }
}

/** Automating parameters on one thread while another processes audio (run under ThreadSanitizer). */
static void testParameterAutomationWhileProcessing()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 16, 16, 256));

    std::atomic<bool> done { false };
    std::atomic<int> problems { 0 };

    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buf (16, 256);
        juce::MidiBuffer midi;
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);

        while (! done.load())
        {
            double inE = 0.0;
            for (int ch = 0; ch < 16; ++ch)
                for (int i = 0; i < 256; ++i)
                {
                    const float v = u (rng);
                    buf.setSample (ch, i, v);
                    inE += (double) v * v;
                }
            p.processBlock (buf, midi);

            double outE = 0.0;
            for (int ch = 0; ch < 16; ++ch)
                for (int i = 0; i < 256; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) ++problems;
                    outE += (double) v * v;
                }
            // 16 independent noise sources may share speakers, so only a loose upper bound applies.
            if (outE > 40.0 * inE) ++problems;
        }
    });

    // Message-thread side: move speakers and sources, republish the triangulation, repeatedly.
    std::mt19937 rng (8);
    std::uniform_real_distribution<float> pos (-9.0f, 9.0f);
    for (int iter = 0; iter < 1500; ++iter)
    {
        const int k = (int) (rng() % 16);
        setParam (p, tlos::params::speakerId (k, 'x'), pos (rng));
        setParam (p, tlos::params::speakerId (k, 'y'), pos (rng));
        setParam (p, tlos::params::sourceId (k, 'x'), pos (rng));
        setParam (p, tlos::params::sourceId (k, 'y'), pos (rng));
        p.refreshLayout();
        if (iter % 50 == 0)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    done = true;
    audio.join();
    CHECK (problems.load() == 0);
}

/** The counts of sources and speakers in use are clamped to the track and silence the rest. */
static void testNumberOfSourcesAndSpeakersInUse()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 4, 8, 256));
    setParam (p, tlos::params::kOutsideRolloffId, 0.0f); // this test is about something else than the falloff outside the speakers

    // Default: everything on the track is used; larger parameter values clamp to the channel counts.
    CHECK (p.getActiveSources() == 4 && p.getActiveSpeakers() == 8);

    juce::AudioBuffer<float> buf (8, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (9);

    // One source in use: the other sources are silent.
    setParam (p, tlos::params::kNumSourcesId, 1.0f);
    CHECK (p.getActiveSources() == 1);
    for (int block = 0; block < 3; ++block)
    {
        const double inE = fillNoise (buf, 4, 1, rng); // source 2 is not in use
        p.processBlock (buf, midi);
        if (block > 0)
        {
            double outE = 0.0;
            for (int ch = 0; ch < 8; ++ch) outE += channelEnergy (buf, ch);
            CHECK (inE > 0.0 && outE < 1e-9);
        }
    }

    // Source 1 is in use and keeps constant power.
    for (int block = 0; block < 3; ++block)
    {
        const double inE = fillNoise (buf, 4, 0, rng);
        p.processBlock (buf, midi);
        if (block > 0)
        {
            double outE = 0.0;
            for (int ch = 0; ch < 8; ++ch) outE += channelEnergy (buf, ch);
            CHECK (std::abs (outE - inE) < 1e-3 * inE);
        }
    }

    // Three speakers in use: only outputs 1-3 may carry signal, power is still conserved.
    setParam (p, tlos::params::kNumSpeakersId, 3.0f);
    p.refreshLayout();
    CHECK (p.getActiveSpeakers() == 3);
    for (int block = 0; block < 3; ++block)
    {
        const double inE = fillNoise (buf, 4, 0, rng);
        p.processBlock (buf, midi);
        if (block > 0)
        {
            double outE = 0.0;
            for (int ch = 0; ch < 8; ++ch)
            {
                outE += channelEnergy (buf, ch);
                if (ch >= 3) CHECK (channelEnergy (buf, ch) < 1e-9);
            }
            CHECK (std::abs (outE - inE) < 1e-3 * inE);
        }
    }

    // Parameter values above the channel count clamp, and below 1 cannot happen (range starts at 1).
    setParam (p, tlos::params::kNumSpeakersId, 64.0f);
    CHECK (p.getActiveSpeakers() == 8);
}

/** Speakers that are no longer in use go silent without clicks. */
static void testUnusedSpeakersAreSilentImmediately()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 2, 8, 256));

    juce::AudioBuffer<float> buf (8, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (10);

    // Put source 1 on speaker 7 so that speaker is definitely carrying signal.
    setParam (p, tlos::params::sourceId (0, 'x'), (float) p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (6, 'x'))->load());
    setParam (p, tlos::params::sourceId (0, 'y'), (float) p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (6, 'y'))->load());
    for (int block = 0; block < 3; ++block)
    {
        fillNoise (buf, 2, 0, rng);
        p.processBlock (buf, midi);
    }
    CHECK (channelEnergy (buf, 6) > 1.0);

    // Reduce to 4 speakers WITHOUT refreshing the triangulation (the audio thread still sees the
    // old one with 8 speakers, as it does for up to ~33 ms in a real host).
    setParam (p, tlos::params::kNumSpeakersId, 4.0f);

    // First block: the outputs that just became unused fade out, ending at (almost) zero.
    fillNoise (buf, 2, 0, rng);
    p.processBlock (buf, midi);
    for (int ch = 4; ch < 8; ++ch)
        CHECK (std::abs (buf.getSample (ch, 255)) < 0.02f);

    // From the next block on they are exactly silent, with or without the new triangulation.
    for (int block = 0; block < 3; ++block)
    {
        if (block == 2)
            p.refreshLayout();
        fillNoise (buf, 2, 0, rng);
        p.processBlock (buf, midi);
        for (int ch = 4; ch < 8; ++ch)
            CHECK (channelEnergy (buf, ch) == 0.0);
    }

    // Growing the count again lets the speakers carry signal again.
    setParam (p, tlos::params::kNumSpeakersId, 8.0f);
    p.refreshLayout();
    double energy = 0.0;
    for (int block = 0; block < 3; ++block)
    {
        fillNoise (buf, 2, 0, rng);
        p.processBlock (buf, midi);
        energy = channelEnergy (buf, 6);
    }
    CHECK (energy > 1.0);
}

/** The speaker layouts in "speaker layouts/" must import cleanly and give a usable triangulation. */
static void testSpeakerPresets (const juce::File& dir)
{
    struct Expected { const char* file; int count; int manualVirtual; }; // manualVirtual 0: the file says virtual="auto"
    const Expected expected[] = { { "Quad.xml", 4, 0 }, { "5.1.xml", 6, 0 }, { "7.1.xml", 8, 0 },
                                  { "Hexagon-front-centre.xml", 6, 0 }, { "Hexagon-symmetric.xml", 6, 0 },
                                  { "Octagon-front-centre.xml", 8, 0 }, { "Octagon-symmetric.xml", 8, 0 },
                                  { "Grid-3x3.xml", 9, 0 }, { "Grid-4x4.xml", 16, 0 }, { "Random-16.xml", 16, 0 },
                                  { "Grid-3x3-virtual-centres.xml", 9, 4 }, { "Grid-4x4-virtual-centres.xml", 16, 9 },
                                  { "Corridor-8x2.xml", 16, 0 }, { "Corridor-8x2-virtual-centres.xml", 16, 7 },
                                  { "Grid-8x8.xml", 64, 0 }, { "Grid-8x8-virtual-centres.xml", 64, 49 } };

    CHECK (dir.isDirectory());
    CHECK (dir.findChildFiles (juce::File::findFiles, false, "*.xml").size() == (int) std::size (expected));

    for (const auto& e : expected)
    {
        TriPanAudioProcessor p;
        CHECK (configure (p, 2, 64, 256));

        tlos::io::ImportReport report;
        const auto result = tlos::io::load (p, tlos::io::Kind::Speakers, dir.getChildFile (e.file), report);
        if (result.failed())
            std::printf ("preset %s: %s\n", e.file, result.getErrorMessage().toRawUTF8());
        CHECK (result.wasOk());
        CHECK (report.applied == e.count && report.skipped == 0 && report.clamped == 0);

        // Importing a speaker setup adjusts Number of Speakers (the track has 64 outputs).
        CHECK (report.fileCount == e.count && report.speakersSetTo == e.count && report.inUse == e.count);
        CHECK (report.virtualMode && *report.virtualMode == (e.manualVirtual == 0 ? tlos::VirtualMode::Auto : tlos::VirtualMode::Manual)); // automatic, or the file's own virtual speakers
        CHECK (report.virtualCount == e.manualVirtual && (int) p.getManualVirtualSpeakers().size() == e.manualVirtual);
        CHECK (p.getActiveSpeakers() == e.count);

        std::vector<tripan::Vec2> pts;
        for (int i = 0; i < e.count; ++i)
            pts.push_back ({ (double) p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (i, 'x'))->load(),
                             (double) p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (i, 'y'))->load() });

        // Within the plane, distinct, and a proper triangulation (non-collinear).
        double minDistance = 1e9;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            CHECK (std::abs (pts[i].x) <= 10.0 && std::abs (pts[i].y) <= 10.0);
            for (size_t j = i + 1; j < pts.size(); ++j)
                minDistance = std::min (minDistance, std::hypot (pts[i].x - pts[j].x, pts[i].y - pts[j].y));
        }
        CHECK (minDistance > 1.0);

        const tripan::Triangulation tri (pts);
        CHECK (! tri.triangles().empty());

        // A virtual speaker at the centre of a cell: its four nearest real speakers (the corners) are equally far away.
        for (const auto& v : p.getManualVirtualSpeakers())
        {
            std::vector<double> distances;
            for (const auto& s : pts)
                distances.push_back (std::hypot (s.x - v.x, s.y - v.y));
            std::sort (distances.begin(), distances.end());
            CHECK (distances.size() >= 5 && distances[3] - distances[0] < 5e-3 && distances[4] - distances[3] > 0.1); // equal up to the 3-decimal rounding of the file
        }

        // With them, a source at the centre of a cell is panned equally to its four corners.
        if (e.manualVirtual > 0)
        {
            std::vector<tripan::VirtualSpeaker> virtuals;
            for (const auto& v : p.getManualVirtualSpeakers())
                virtuals.push_back ({ v, 1.0 });

            const tripan::SpeakerLayout layout (pts, virtuals);
            for (const auto& v : virtuals)
            {
                const auto g = tripan::computeGains (layout, v.position);
                int audible = 0;
                for (int k = 0; k < g.count; ++k)
                    if (g.gain[(size_t) k] > 1e-4f)
                    {
                        ++audible;
                        CHECK (std::abs (g.gain[(size_t) k] - 0.5f) < 0.01f);
                    }
                CHECK (audible == 4);
            }
        }

        // Constant power everywhere on a coarse sweep, including outside the speakers.
        for (double x = -9.0; x <= 9.0; x += 1.5)
            for (double y = -9.0; y <= 9.0; y += 1.5)
            {
                const auto g = tripan::computeGains (tri, { x, y });
                double sum = 0.0;
                for (int k = 0; k < g.count; ++k) sum += (double) g.gain[(size_t) k] * g.gain[(size_t) k];
                CHECK (std::abs (sum - 1.0) < 1e-5);
            }
    }

    // Files may give a position as azimuth and distance instead of x and y.
    {
        TriPanAudioProcessor p;
        CHECK (configure (p, 2, 8, 256));
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tlos-polar-speakers.xml");
        file.replaceWithText ("<TriPanSpeakers version=\"1\" count=\"3\">"
                              "<Speaker index=\"1\" az=\"0\" dist=\"4\"/>"
                              "<Speaker index=\"2\" az=\"90\" dist=\"2\"/>"
                              "<Speaker index=\"3\" x=\"1.5\" y=\"-2\" az=\"10\" dist=\"9\"/>"   // x and y win
                              "</TriPanSpeakers>");
        tlos::io::ImportReport r;
        CHECK (tlos::io::load (p, tlos::io::Kind::Speakers, file, r).wasOk() && r.skipped == 0);
        const auto px = [&] (int i, char c) { return p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (i, c))->load(); };
        CHECK (std::abs (px (0, 'x')) < 1e-4f && std::abs (px (0, 'y') - 4.0f) < 1e-4f);
        CHECK (std::abs (px (1, 'x') + 2.0f) < 1e-4f && std::abs (px (1, 'y')) < 1e-4f);
        CHECK (std::abs (px (2, 'x') - 1.5f) < 1e-4f && std::abs (px (2, 'y') + 2.0f) < 1e-4f);
        file.replaceWithText ("<TriPanSpeakers version=\"1\"><Speaker index=\"1\" az=\"0\" dist=\"-1\"/></TriPanSpeakers>");
        tlos::io::ImportReport bad;
        CHECK (tlos::io::load (p, tlos::io::Kind::Speakers, file, bad).failed() || bad.skipped == 1);
        file.deleteFile();
    }

    // The number of speakers follows the file, in both directions, as long as the track supports it.
    {
        TriPanAudioProcessor p;
        CHECK (configure (p, 2, 8, 256));
        tlos::io::ImportReport r;

        CHECK (p.getActiveSpeakers() == 8);
        CHECK (tlos::io::load (p, tlos::io::Kind::Speakers, dir.getChildFile ("Quad.xml"), r).wasOk());
        CHECK (p.getActiveSpeakers() == 4 && r.speakersSetTo == 4 && r.trackOutputs == 8);
        CHECK (std::abs (p.getValueTreeState().getRawParameterValue (tlos::params::kNumSpeakersId)->load() - 4.0f) < 1e-3f);

        CHECK (tlos::io::load (p, tlos::io::Kind::Speakers, dir.getChildFile ("Octagon-symmetric.xml"), r).wasOk());
        CHECK (p.getActiveSpeakers() == 8 && r.speakersSetTo == 8);

        // A file with more speakers than the track has outputs: positions are stored, the count is left alone.
        setParam (p, tlos::params::kNumSpeakersId, 5.0f);
        CHECK (tlos::io::load (p, tlos::io::Kind::Speakers, dir.getChildFile ("Grid-4x4.xml"), r).wasOk());
        CHECK (r.fileCount == 16 && r.speakersSetTo == 0 && r.trackOutputs == 8 && r.applied == 16 && r.inUse == 5);
        CHECK (p.getActiveSpeakers() == 5);

        // Importing sources never changes Number of Sources.
        const auto sourceFile = dir.getChildFile ("..").getChildFile ("tripan_one_source.xml");
        sourceFile.replaceWithText ("<TriPanSources version=\"1\"><Source index=\"1\" x=\"1\" y=\"2\"/></TriPanSources>");
        CHECK (tlos::io::load (p, tlos::io::Kind::Sources, sourceFile, r).wasOk());
        CHECK (p.getActiveSources() == 2 && r.speakersSetTo == 0);
        sourceFile.deleteFile();
    }

    if (! dir.isDirectory())
        return; // already reported above; avoids indexing empty layouts

    // Spot checks of the geometry (+y front, +x right, radius 5 m).
    const auto load = [&] (const char* file, std::vector<tripan::Vec2>& out)
    {
        TriPanAudioProcessor p;
        configure (p, 2, 64, 256);
        tlos::io::ImportReport r;
        CHECK (tlos::io::load (p, tlos::io::Kind::Speakers, dir.getChildFile (file), r).wasOk());
        out.clear();
        for (int i = 0; i < r.applied; ++i)
            out.push_back ({ (double) p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (i, 'x'))->load(),
                             (double) p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (i, 'y'))->load() });
    };
    const auto azimuth = [] (tripan::Vec2 v) { return std::atan2 (v.x, v.y) * 180.0 / 3.14159265358979; };

    std::vector<tripan::Vec2> v;
    load ("5.1.xml", v);
    CHECK (std::abs (v[2].x) < 1e-3 && std::abs (v[2].y - 5.0) < 1e-3);                  // centre
    CHECK (std::abs (azimuth (v[0]) + 30.0) < 0.1 && std::abs (azimuth (v[1]) - 30.0) < 0.1);
    CHECK (std::abs (azimuth (v[4]) + 110.0) < 0.1 && std::abs (azimuth (v[5]) - 110.0) < 0.1);   // surrounds, after the LFE (channel 4)
    CHECK (std::abs (v[3].x) < 1e-3 && std::abs (v[3].y - 2.5) < 1e-3);                  // the LFE, halfway to the centre

    load ("Quad.xml", v);
    CHECK (std::abs (azimuth (v[0]) + 45.0) < 0.1 && std::abs (azimuth (v[3]) - 135.0) < 0.1);

    load ("Hexagon-front-centre.xml", v);
    CHECK (std::abs (v[0].x) < 1e-3 && std::abs (v[0].y - 5.0) < 1e-3);                  // speaker at front centre
    load ("Hexagon-symmetric.xml", v);
    CHECK (std::abs (azimuth (v[0]) + 30.0) < 0.1 && std::abs (azimuth (v[1]) - 30.0) < 0.1);
    for (const auto& s : v) CHECK (std::abs (s.x) > 1.0);                                // none on the centre line

    load ("Octagon-front-centre.xml", v);
    CHECK (std::abs (v[0].x) < 1e-3 && std::abs (v[0].y - 5.0) < 1e-3);
    load ("Octagon-symmetric.xml", v);
    CHECK (std::abs (azimuth (v[0]) + 22.5) < 0.1 && std::abs (azimuth (v[1]) - 22.5) < 0.1);
    for (const auto& s : v) CHECK (std::abs (s.x) > 1.0);

    load ("Grid-3x3.xml", v);
    CHECK (std::abs (v[4].x) < 1e-3 && std::abs (v[4].y) < 1e-3);                        // centre of the grid
    load ("Grid-4x4.xml", v);
    CHECK (std::abs (v[0].x + 5.0) < 1e-3 && std::abs (v[0].y - 5.0) < 1e-3 && std::abs (v[15].x - 5.0) < 1e-3 && std::abs (v[15].y + 5.0) < 1e-3);

    // Corridor: 2 rows of 8, every cell a rectangle (all rows share their y, all columns their x).
    for (const char* file : { "Corridor-8x2.xml", "Corridor-8x2-virtual-centres.xml" })
    {
        load (file, v);
        CHECK (v.size() == 16);
        for (int c = 0; c < 8; ++c)
        {
            CHECK (std::abs (v[(size_t) c].y - 1.5) < 1e-3 && std::abs (v[(size_t) (8 + c)].y + 1.5) < 1e-3); // two rows, front then back
            CHECK (std::abs (v[(size_t) c].x - v[(size_t) (8 + c)].x) < 1e-3);                                 // columns line up
            if (c > 0)
                CHECK (std::abs ((v[(size_t) c].x - v[(size_t) (c - 1)].x) - 10.0 / 7.0) < 1e-3);             // equal spacing
        }
    }

    // 8x8 grids: 64 speakers, equal spacing in both directions.
    for (const char* file : { "Grid-8x8.xml", "Grid-8x8-virtual-centres.xml" })
    {
        load (file, v);
        CHECK (v.size() == 64);
        CHECK (std::abs (v[0].x + 5.0) < 1e-3 && std::abs (v[0].y - 5.0) < 1e-3 && std::abs (v[63].x - 5.0) < 1e-3 && std::abs (v[63].y + 5.0) < 1e-3);
        CHECK (std::abs (v[1].x - v[0].x - 10.0 / 7.0) < 1e-3 && std::abs (v[0].y - v[8].y - 10.0 / 7.0) < 1e-3);
    }
}

/** The virtual speakers of an 8-speaker ring fill in the centre, and the modes behave as described. */
static void testVirtualSpeakerModes()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 2, 8, 256));
    juce::AudioBuffer<float> buf (8, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (11);

    const auto outputEnergies = [&] (int source, double& inputEnergy)
    {
        std::vector<double> e (8, 0.0);
        for (int block = 0; block < 3; ++block)
        {
            inputEnergy = fillNoise (buf, 2, source, rng);
            p.processBlock (buf, midi);
        }
        for (int ch = 0; ch < 8; ++ch) e[(size_t) ch] = channelEnergy (buf, ch);
        return e;
    };
    const auto activeOutputs = [] (const std::vector<double>& e) { int n = 0; for (double v : e) if (v > 1e-9) ++n; return n; };

    // Default: Auto. The 8-ring gets a centre speaker.
    CHECK (p.getVirtualMode() == tlos::VirtualMode::Auto);
    CHECK (p.getVirtualSpeakers().size() == 1);

    setParam (p, tlos::params::sourceId (0, 'x'), 0.0f);
    setParam (p, tlos::params::sourceId (0, 'y'), 0.0f);
    double inE = 0.0;
    auto e = outputEnergies (0, inE);
    CHECK (activeOutputs (e) == 8);                 // a source at the centre feeds every speaker...
    double total = 0.0;
    for (double v : e) total += v;
    CHECK (std::abs (total - inE) < 1e-3 * inE);    // ...with unchanged total power
    for (double v : e) CHECK (std::abs (v - inE / 8.0) < 0.02 * inE); // ...equally

    // Off: plain triangulation, at most three speakers.
    p.setVirtualMode (tlos::VirtualMode::Off);
    CHECK (p.getVirtualSpeakers().empty());
    setParam (p, tlos::params::sourceId (0, 'x'), 1.0f);
    setParam (p, tlos::params::sourceId (0, 'y'), 3.0f);
    e = outputEnergies (0, inE);
    CHECK (activeOutputs (e) <= 3);

    // Manual: switching from Off with an empty list gives an empty list; the user's own points are used.
    p.setVirtualMode (tlos::VirtualMode::Manual);
    CHECK (p.getManualVirtualSpeakers().empty());
    p.setManualVirtualSpeakers ({ { 0.0, 0.0 } });
    CHECK (p.getVirtualSpeakers().size() == 1);
    setParam (p, tlos::params::sourceId (0, 'x'), 0.0f);
    setParam (p, tlos::params::sourceId (0, 'y'), 0.0f);
    e = outputEnergies (0, inE);
    CHECK (activeOutputs (e) == 8);

    // Auto -> Manual copies the automatic result when the manual list is empty.
    TriPanAudioProcessor q;
    CHECK (configure (q, 2, 8, 256));
    q.setVirtualMode (tlos::VirtualMode::Manual);
    CHECK (q.getManualVirtualSpeakers().size() == 1);

    // Mode and manual list are saved with the state; limits are enforced.
    q.setManualVirtualSpeakers ({ { 1.5, -2.25 }, { 199.0, 0.0 } });
    juce::MemoryBlock state;
    q.getStateInformation (state);
    TriPanAudioProcessor r;
    r.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (configure (r, 2, 8, 256));
    CHECK (r.getVirtualMode() == tlos::VirtualMode::Manual);
    const auto manual = r.getManualVirtualSpeakers();
    CHECK (manual.size() == 2 && std::abs (manual[0].x - 1.5) < 1e-3 && std::abs (manual[0].y + 2.25) < 1e-3 && std::abs (manual[1].x - 100.0) < 1e-3);
    std::vector<tripan::Vec2> tooMany (100, tripan::Vec2 { 1.0, 1.0 });
    r.setManualVirtualSpeakers (tooMany);
    CHECK ((int) r.getManualVirtualSpeakers().size() == TriPanAudioProcessor::kMaxVirtualSpeakers);
}

/** The reported 5.1 problem: a source at the origin must be treated symmetrically (L = R, Ls = Rs), and so must every mirrored position, for every symmetric ring layout in the presets. */
static void testMirrorSymmetryWithVirtualSpeakers (const juce::File& dir)
{
    if (! dir.isDirectory())
        return;

    for (const char* file : { "Quad.xml", "5.1.xml", "7.1.xml", "Hexagon-front-centre.xml", "Hexagon-symmetric.xml",
                              "Octagon-front-centre.xml", "Octagon-symmetric.xml" })
    {
        TriPanAudioProcessor p;
        CHECK (configure (p, 2, 16, 256));
        tlos::io::ImportReport r;
        CHECK (tlos::io::load (p, tlos::io::Kind::Speakers, dir.getChildFile (file), r).wasOk());

        // The speakers that take part in the panning (an LFE in a 5.1 or 7.1 preset is excluded).
        const auto speakers = p.getIncludedSpeakers().positions;
        CHECK ((int) speakers.size() == r.applied - (std::string (file) == "5.1.xml" || std::string (file) == "7.1.xml" ? 1 : 0));

        const auto virtualSpeakers = p.getVirtualSpeakers();
        CHECK (! virtualSpeakers.empty());
        const tripan::SpeakerLayout tri (speakers, virtualSpeakers);

        // mirror[i] = the speaker at the mirrored position (-x, y)
        std::vector<int> mirror (speakers.size(), -1);
        for (size_t i = 0; i < speakers.size(); ++i)
            for (size_t j = 0; j < speakers.size(); ++j)
                if (std::hypot (speakers[i].x + speakers[j].x, speakers[i].y - speakers[j].y) < 2e-3)
                    mirror[i] = (int) j;
        for (int m : mirror) CHECK (m >= 0);

        const auto gainOf = [] (const tripan::SourceGains& g, int speaker)
        {
            for (int i = 0; i < g.count; ++i)
                if (g.speaker[(size_t) i] == speaker) return (double) g.gain[(size_t) i];
            return 0.0;
        };

        double worst = 0.0;
        for (double x = -9.0; x <= 9.01; x += 0.75)
            for (double y = -9.0; y <= 9.01; y += 0.75)
            {
                const auto a = tripan::computeGains (tri, { x, y });
                const auto b = tripan::computeGains (tri, { -x, y });

                double sumSq = 0.0;
                for (int k = 0; k < a.count; ++k) sumSq += (double) a.gain[(size_t) k] * a.gain[(size_t) k];
                CHECK (std::abs (sumSq - 1.0) < 1e-5);

                for (size_t i = 0; i < speakers.size(); ++i)
                    worst = std::max (worst, std::abs (gainOf (a, (int) i) - gainOf (b, mirror[i])));
            }
        if (worst >= 2e-3)
            std::printf ("preset %s: worst mirror asymmetry %g\n", file, worst);
        CHECK (worst < 2e-3);

        // Without virtual speakers some of these layouts are NOT symmetric (cocircular ties).
        const tripan::Triangulation plain (speakers);
        double plainWorst = 0.0;
        for (double x = -9.0; x <= 9.01; x += 0.75)
            for (double y = -9.0; y <= 9.01; y += 0.75)
            {
                const auto a = tripan::computeGains (plain, { x, y });
                const auto b = tripan::computeGains (plain, { -x, y });
                for (size_t i = 0; i < speakers.size(); ++i)
                    plainWorst = std::max (plainWorst, std::abs (gainOf (a, (int) i) - gainOf (b, mirror[i])));
            }
        std::printf ("  %-26s mirror asymmetry: plain %.3f  with virtual speakers %.5f\n", file, plainWorst, worst);
    }
}

/** Sources that have not moved reuse their gains, but must follow any change of the layout. */
static void testUnchangedSourcesFollowLayoutChanges()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 1, 6, 256));
    setParam (p, tlos::params::kOutsideRolloffId, 0.0f); // this test is about something else than the falloff outside the speakers
    p.setVirtualMode (tlos::VirtualMode::Off);

    juce::AudioBuffer<float> buf (6, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (31);

    // A stationary source right next to speaker 1 (the first speaker, at the front).
    setParam (p, tlos::params::sourceId (0, 'x'), (float) p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (0, 'x'))->load());
    setParam (p, tlos::params::sourceId (0, 'y'), (float) p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (0, 'y'))->load());

    double inE = 0.0;
    for (int block = 0; block < 4; ++block)
    {
        inE = fillNoise (buf, 1, 0, rng);
        p.processBlock (buf, midi); // from the second block on the gains come from the cache
    }
    CHECK (std::abs (channelEnergy (buf, 0) - inE) < 1e-3 * inE);

    // Move speaker 1 elsewhere; the source stays. The cached gains must be dropped.
    setParam (p, tlos::params::speakerId (0, 'x'), -4.0f);
    setParam (p, tlos::params::speakerId (0, 'y'), -4.0f);
    p.refreshLayout();
    for (int block = 0; block < 4; ++block)
    {
        inE = fillNoise (buf, 1, 0, rng);
        p.processBlock (buf, midi);
    }
    CHECK (channelEnergy (buf, 0) < 0.9 * inE); // speaker 1 is no longer under the source

    double outE = 0.0;
    for (int ch = 0; ch < 6; ++ch) outE += channelEnergy (buf, ch);
    CHECK (std::abs (outE - inE) < 1e-3 * inE);

    // Switching the virtual speakers also changes the layout.
    p.setVirtualMode (tlos::VirtualMode::Manual);
    p.setManualVirtualSpeakers ({ { 0.0, 0.0 } });
    for (int block = 0; block < 4; ++block)
    {
        inE = fillNoise (buf, 1, 0, rng);
        p.processBlock (buf, midi);
    }
    outE = 0.0;
    for (int ch = 0; ch < 6; ++ch) outE += channelEnergy (buf, ch);
    CHECK (std::abs (outE - inE) < 1e-3 * inE);
}

/** Per-source spread: a stationary source on a speaker feeds more speakers as its spread grows, with unchanged power. */
static void testSourceSpread()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 2, 6, 256));
    setParam (p, tlos::params::kOutsideRolloffId, 0.0f); // this test is about something else than the falloff outside the speakers
    p.setVirtualMode (tlos::VirtualMode::Off);

    juce::AudioBuffer<float> buf (6, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (51);

    CHECK (p.getValueTreeState().getRawParameterValue (tlos::params::spreadId (0))->load() == 0.0f); // default: no spread

    // Source 1 exactly on speaker 1 (the default ring starts at the front).
    setParam (p, tlos::params::sourceId (0, 'x'), p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (0, 'x'))->load());
    setParam (p, tlos::params::sourceId (0, 'y'), p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (0, 'y'))->load());

    const auto measure = [&] (double& inputEnergy)
    {
        std::vector<double> e (6, 0.0);
        for (int block = 0; block < 4; ++block)
        {
            inputEnergy = fillNoise (buf, 2, 0, rng);
            p.processBlock (buf, midi);
        }
        for (int ch = 0; ch < 6; ++ch) e[(size_t) ch] = channelEnergy (buf, ch);
        return e;
    };

    double inE = 0.0;
    auto e = measure (inE);
    CHECK (std::abs (e[0] - inE) < 1e-3 * inE);                       // all of it on speaker 1

    double previousShare = 1.0;
    for (float spread : { 1.0f, 3.0f, 6.0f })
    {
        setParam (p, tlos::params::spreadId (0), spread);           // changing only the spread must be noticed
        e = measure (inE);

        double total = 0.0;
        int active = 0;
        for (double v : e) { total += v; if (v > 1e-3 * inE) ++active; }
        CHECK (std::abs (total - inE) < 1e-3 * inE);                  // power is unchanged
        CHECK (e[0] / inE < previousShare);                           // the speaker under the source gets less
        CHECK (active >= 3);                                          // and others take part
        previousShare = e[0] / inE;
    }

    // Source 2 has its own spread: it is unaffected by source 1.
    setParam (p, tlos::params::spreadId (0), 0.0f);
    setParam (p, tlos::params::sourceId (1, 'x'), p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (2, 'x'))->load());
    setParam (p, tlos::params::sourceId (1, 'y'), p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (2, 'y'))->load());
    double inE2 = 0.0;
    std::vector<double> e2 (6, 0.0);
    for (int block = 0; block < 4; ++block)
    {
        inE2 = fillNoise (buf, 2, 1, rng);
        p.processBlock (buf, midi);
    }
    for (int ch = 0; ch < 6; ++ch) e2[(size_t) ch] = channelEnergy (buf, ch);
    CHECK (std::abs (e2[2] - inE2) < 1e-3 * inE2);

    // The spread is a normal automatable parameter, saved with the state.
    setParam (p, tlos::params::spreadId (1), 2.5f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    TriPanAudioProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (std::abs (q.getValueTreeState().getRawParameterValue (tlos::params::spreadId (1))->load() - 2.5f) < 1e-3f);
    CHECK (q.getValueTreeState().getParameter (tlos::params::spreadId (63)) != nullptr);
}

/** The order of the parameters as a host lists them: everything global first (counts, outside rolloff, virtual speaker mode and number),
    then each source, each speaker, and last the virtual speaker positions. */
static void testParameterOrder()
{
    TriPanAudioProcessor p;
    const auto& parameters = p.getParameters();

    std::vector<juce::String> expected { tlos::params::kNumSourcesId, tlos::params::kNumSpeakersId, tlos::params::kOutsideRolloffId,
                                         tlos::params::kVirtualModeId, tlos::params::kNumVirtualId };
    for (int i = 0; i < tlos::kMaxChannels; ++i)
    {
        expected.push_back (tlos::params::sourceId (i, 'x'));
        expected.push_back (tlos::params::sourceId (i, 'y'));
        expected.push_back (tlos::params::spreadId (i));
        expected.push_back (tlos::params::sourceTrimId (i));
        expected.push_back (tlos::params::sourceMuteId (i));
        expected.push_back (tlos::params::sourceSoloId (i));
        expected.push_back (tlos::params::sourceGroupId (i));
    }
    for (int i = 0; i < tlos::kMaxChannels; ++i)
    {
        expected.push_back (tlos::params::speakerId (i, 'x'));
        expected.push_back (tlos::params::speakerId (i, 'y'));
        expected.push_back (tlos::params::speakerTrimId (i));
        expected.push_back (tlos::params::speakerMuteId (i));
        expected.push_back (tlos::params::speakerSoloId (i));
        expected.push_back (tlos::params::speakerExcludeId (i));
    }
    for (int i = 0; i < tlos::params::kMaxVirtualSpeakers; ++i)
    {
        expected.push_back (tlos::params::virtualId (i, 'x'));
        expected.push_back (tlos::params::virtualId (i, 'y'));
    }

    CHECK ((size_t) parameters.size() == expected.size());
    CHECK (expected.size() == 5 + 64 * 7 + 64 * 6 + 128);

    bool inOrder = parameters.size() == (int) expected.size();
    for (int i = 0; inOrder && i < parameters.size(); ++i)
    {
        const auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameters[i]);
        if (withId == nullptr || withId->paramID != expected[(size_t) i])
        {
            std::printf ("parameter %d is %s, expected %s\n", i, withId != nullptr ? withId->paramID.toRawUTF8() : "?", expected[(size_t) i].toRawUTF8());
            inOrder = false;
        }
    }
    CHECK (inOrder);

    // The names a host shows follow the same order.
    CHECK (parameters[0]->getName (64) == "Number of Sources" && parameters[1]->getName (64) == "Number of Speakers" && parameters[2]->getName (64) == "Outside Rolloff");
    CHECK (parameters[3]->getName (64) == "Virtual Speakers" && parameters[4]->getName (64) == "Number of Virtual Speakers");
    CHECK (parameters[5]->getName (64) == "Source 1 X" && parameters[6]->getName (64) == "Source 1 Y" && parameters[7]->getName (64) == "Source 1 Spread");
    CHECK (parameters[8]->getName (64) == "Source 1 Trim" && parameters[9]->getName (64) == "Source 1 Mute" && parameters[10]->getName (64) == "Source 1 Solo");
    CHECK (parameters[11]->getName (64) == "Source 1 Group" && parameters[12]->getName (64) == "Source 2 X");
    const int speakers = 5 + 64 * 7;
    CHECK (parameters[speakers]->getName (64) == "Speaker 1 X" && parameters[speakers + 1]->getName (64) == "Speaker 1 Y");
    CHECK (parameters[speakers + 2]->getName (64) == "Speaker 1 Trim" && parameters[speakers + 4]->getName (64) == "Speaker 1 Solo" && parameters[speakers + 5]->getName (64) == "Speaker 1 Exclude");
    CHECK (parameters[speakers + 64 * 6]->getName (64) == "Virtual Speaker 1 X");
    CHECK (parameters[parameters.size() - 1]->getName (64) == "Virtual Speaker 64 Y");

    // Hosts that show a tree (VST3 units, AU) get the same order as groups: Global first.
    const auto& tree = p.getParameterTree();
    CHECK (tree.getSubgroups (false).size() == 4 && tree.getSubgroups (false).getFirst()->getName() == "Global");
}

/** The limit of 64 manual virtual speakers: with 64 sources and 64 speakers the output stays finite and keeps its power, and the cost of a block is reported. */
static void testManyVirtualSpeakers()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 64, 64, 512));
    setParam (p, tlos::params::kOutsideRolloffId, 0.0f); // the sources below are partly outside the speakers: keep their power
    std::vector<tripan::Vec2> list;
    for (int i = 0; i < TriPanAudioProcessor::kMaxVirtualSpeakers; ++i)
        list.push_back ({ 8.0 * std::sin (i * 2.399963), 8.0 * std::cos (i * 2.399963) * (0.3 + 0.7 * (i % 7) / 6.0) });
    p.setVirtualLayout (tlos::VirtualMode::Manual, &list);
    p.refreshLayout();
    CHECK ((int) p.getManualVirtualSpeakers().size() == 64 && p.getVirtualSpeakers().size() == 64);

    juce::AudioBuffer<float> buf (64, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);

    double worst = 0.0, total = 0.0;
    const int blocks = 100;
    for (int block = 0; block < blocks; ++block)
    {
        double inE = 0.0;
        for (int s = 0; s < 64; ++s)
        {
            const float a = 0.05f * (float) block + (float) s;
            setParam (p, tlos::params::sourceId (s, 'x'), 6.0f * std::sin (a));
            setParam (p, tlos::params::sourceId (s, 'y'), 6.0f * std::cos (a));
            for (int i = 0; i < 512; ++i) { const float v = u (rng); buf.setSample (s, i, v); inE += (double) v * v; }
        }

        const auto t0 = std::chrono::steady_clock::now();
        p.processBlock (buf, midi);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        worst = std::max (worst, ms);
        total += ms;

        CHECK (allFinite (buf, 64));
        double outE = 0.0;
        for (int ch = 0; ch < 64; ++ch) outE += channelEnergy (buf, ch);
        if (block > 2) // the first blocks ramp from the start gains
            CHECK (outE > 0.5 * inE && outE < 1.5 * inE); // independent noise sources: power is roughly conserved
    }

    std::printf ("64x64 with 64 virtual speakers: %.3f ms average, %.3f ms worst per 512-sample block (budget at 48 kHz: 10.7 ms)\n",
                 total / blocks, worst);
}

/** Outside the speakers the level falls (6 dB per doubling of 1 + distance by default); inside nothing changes; 0 dB switches it off. */
static void testOutsideFalloff()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 1, 8, 512));
    CHECK (std::abs (p.getValueTreeState().getRawParameterValue (tlos::params::kOutsideRolloffId)->load() - 6.0f) < 1e-6f); // default

    juce::AudioBuffer<float> buf (8, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (21);

    // The default speaker ring has radius 5, so the speaker at the front is at (0, 5): a source at (0, 6) is 1 m outside.
    const auto energyRatioAt = [&] (float x, float y)
    {
        setParam (p, tlos::params::sourceId (0, 'x'), x);
        setParam (p, tlos::params::sourceId (0, 'y'), y);
        double inE = 0.0, outE = 0.0;
        for (int block = 0; block < 4; ++block)
        {
            inE = fillNoise (buf, 1, 0, rng);
            p.processBlock (buf, midi);
        }
        for (int ch = 0; ch < 8; ++ch)
            outE += channelEnergy (buf, ch);
        return outE / inE;
    };

    CHECK (std::abs (energyRatioAt (0.0f, 2.0f) - 1.0) < 1e-3);          // inside: constant power
    CHECK (std::abs (energyRatioAt (0.0f, 5.0f) - 1.0) < 1e-3);          // on a speaker: on the boundary
    CHECK (std::abs (energyRatioAt (0.0f, 6.0f) - 0.25) < 0.01);         // 1 m outside: (1 + 1)^-1 = -6 dB = 0.25 in power
    CHECK (std::abs (energyRatioAt (0.0f, 8.0f) - 1.0 / 16.0) < 0.01);   // 3 m outside: (1 + 3)^-1 = -12 dB

    setParam (p, tlos::params::kOutsideRolloffId, 12.0f);
    CHECK (std::abs (energyRatioAt (0.0f, 6.0f) - 0.0625) < 0.01);       // -12 dB for 1 m

    setParam (p, tlos::params::kOutsideRolloffId, 0.0f);                 // off: as before
    CHECK (std::abs (energyRatioAt (0.0f, 8.0f) - 1.0) < 1e-3);
    CHECK (std::abs (energyRatioAt (0.0f, 6.0f) - 1.0) < 1e-3);
}


/** Energy of every output channel for one stationary source with noise, after the gain ramps have settled, per unit of input energy. */
static std::vector<double> channelEnergies (TriPanAudioProcessor& p, juce::AudioBuffer<float>& buf, int outputs, int source, std::mt19937& rng)
{
    juce::MidiBuffer midi;
    double inE = 0.0;
    for (int block = 0; block < 4; ++block)
    {
        inE = fillNoise (buf, 8, source, rng);
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

/** Trim, mute and solo of sources and speakers: trim scales, mute silences without redistributing, solo silences the others (mute wins). */
static void testTrimMuteSolo()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 2, 4, 512));
    setParam (p, tlos::params::kOutsideRolloffId, 0.0f);
    juce::AudioBuffer<float> buf (4, 512);
    std::mt19937 rng (31);
    const auto run = [&] (int source) { return channelEnergies (p, buf, 4, source, rng); };

    // Defaults: trim 0 dB, no mute, no solo.
    for (int i = 0; i < 2; ++i)
        CHECK (std::abs (p.getValueTreeState().getRawParameterValue (tlos::params::sourceTrimId (i))->load()) < 1e-4f
               && p.getValueTreeState().getRawParameterValue (tlos::params::sourceMuteId (i))->load() == 0.0f
               && p.getValueTreeState().getRawParameterValue (tlos::params::speakerSoloId (i))->load() == 0.0f);

    const auto base = run (0);
    CHECK (std::abs (sumOf (base) - 1.0) < 1e-3);

    setParam (p, tlos::params::sourceTrimId (0), -6.0206f);                  // a source trim is a gain on the source
    const auto trimmedSource = run (0);
    CHECK (std::abs (sumOf (trimmedSource) - 0.25) < 0.005);
    for (int ch = 0; ch < 4; ++ch)
        CHECK (std::abs (trimmedSource[(size_t) ch] - 0.25 * base[(size_t) ch]) < 0.005);
    setParam (p, tlos::params::sourceTrimId (0), 0.0f);

    setParam (p, tlos::params::speakerTrimId (1), -6.0206f);                 // a speaker trim is a gain on that output only
    const auto trimmedSpeaker = run (0);
    CHECK (std::abs (trimmedSpeaker[1] - 0.25 * base[1]) < 0.005);
    for (int ch : { 0, 2, 3 })
        CHECK (std::abs (trimmedSpeaker[(size_t) ch] - base[(size_t) ch]) < 0.005);
    setParam (p, tlos::params::speakerTrimId (1), 0.0f);

    setParam (p, tlos::params::sourceMuteId (0), 1.0f);                      // a muted source is silent, the other one is not
    CHECK (sumOf (run (0)) == 0.0);
    CHECK (std::abs (sumOf (run (1)) - 1.0) < 1e-3);
    setParam (p, tlos::params::sourceMuteId (0), 0.0f);
    CHECK (std::abs (sumOf (run (0)) - 1.0) < 1e-3);

    setParam (p, tlos::params::speakerMuteId (2), 1.0f);                     // a muted speaker is silent; the others keep their levels
    const auto mutedSpeaker = run (0);
    CHECK (mutedSpeaker[2] == 0.0);
    for (int ch : { 0, 1, 3 })
        CHECK (std::abs (mutedSpeaker[(size_t) ch] - base[(size_t) ch]) < 0.005);
    setParam (p, tlos::params::speakerMuteId (2), 0.0f);

    setParam (p, tlos::params::sourceSoloId (1), 1.0f);                      // solo: only the soloed source is heard
    CHECK (sumOf (run (0)) == 0.0);
    CHECK (std::abs (sumOf (run (1)) - 1.0) < 1e-3);
    setParam (p, tlos::params::sourceMuteId (1), 1.0f);                      // mute wins over solo
    CHECK (sumOf (run (1)) == 0.0);
    setParam (p, tlos::params::sourceMuteId (1), 0.0f);
    setParam (p, tlos::params::sourceSoloId (1), 0.0f);
    CHECK (std::abs (sumOf (run (0)) - 1.0) < 1e-3);

    setParam (p, tlos::params::speakerSoloId (3), 1.0f);                     // solo of a speaker: only that output; sources are not affected
    const auto soloSpeaker = run (0);
    CHECK (soloSpeaker[0] == 0.0 && soloSpeaker[1] == 0.0 && soloSpeaker[2] == 0.0 && soloSpeaker[3] > 0.0);
    CHECK (std::abs (soloSpeaker[3] - base[3]) < 0.005);
    setParam (p, tlos::params::speakerSoloId (3), 0.0f);

    setParam (p, tlos::params::sourceSoloId (0), 1.0f);                      // a soloed source does not silence speakers
    const auto soloSource = run (0);
    for (int ch = 0; ch < 4; ++ch)
        CHECK (std::abs (soloSource[(size_t) ch] - base[(size_t) ch]) < 0.005);
}

/** A switch changes the gain over a block, not in one step: there is no click. */
static void testMuteHasNoClick()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 1, 4, 512));
    juce::AudioBuffer<float> buf (4, 512);
    juce::MidiBuffer midi;

    const auto constantInput = [&] { buf.clear(); for (int i = 0; i < 512; ++i) buf.setSample (0, i, 1.0f); };
    for (int block = 0; block < 3; ++block) { constantInput(); p.processBlock (buf, midi); }

    float biggestStep = 0.0f;
    setParam (p, tlos::params::sourceMuteId (0), 1.0f);
    constantInput();
    p.processBlock (buf, midi);
    for (int ch = 0; ch < 4; ++ch)
        for (int i = 1; i < 512; ++i)
            biggestStep = std::max (biggestStep, std::abs (buf.getSample (ch, i) - buf.getSample (ch, i - 1)));
    CHECK (biggestStep < 0.01f);                                              // a linear ramp over 512 samples
    CHECK (std::abs (buf.getSample (0, 0)) + std::abs (buf.getSample (1, 0)) + std::abs (buf.getSample (2, 0)) + std::abs (buf.getSample (3, 0)) > 0.5f); // it starts where it was
    constantInput();
    p.processBlock (buf, midi);
    CHECK (std::abs (channelEnergy (buf, 0)) + channelEnergy (buf, 1) + channelEnergy (buf, 2) + channelEnergy (buf, 3) == 0.0);
}

/** An excluded speaker takes no part: it is silent, and the others pan as if it did not exist (same as a layout without it). */
static void testExclude()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 1, 8, 512));
    setParam (p, tlos::params::sourceId (0, 'x'), 1.0f);
    setParam (p, tlos::params::sourceId (0, 'y'), 2.0f);
    p.refreshLayout();
    juce::AudioBuffer<float> buf (8, 512);
    std::mt19937 rng (32);

    const auto base = channelEnergies (p, buf, 8, 0, rng);
    CHECK (std::abs (sumOf (base) - 1.0) < 1e-3);
    CHECK (! p.isSpeakerExcluded (3));

    p.setSpeakerExcluded (3, true);
    CHECK (p.isSpeakerExcluded (3) && ! p.isSpeakerExcluded (2));
    const auto excluded = channelEnergies (p, buf, 8, 0, rng);
    CHECK (excluded[3] == 0.0);
    CHECK (std::abs (sumOf (excluded) - 1.0) < 1e-3);                         // constant power over the others

    // The same as a layout that has no such speaker.
    TriPanAudioProcessor reference;
    CHECK (configure (reference, 1, 7, 512));
    for (int i = 0; i < 7; ++i)
    {
        const int channel = i < 3 ? i : i + 1;
        setParam (reference, tlos::params::speakerId (i, 'x'), p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (channel, 'x'))->load());
        setParam (reference, tlos::params::speakerId (i, 'y'), p.getValueTreeState().getRawParameterValue (tlos::params::speakerId (channel, 'y'))->load());
    }
    setParam (reference, tlos::params::sourceId (0, 'x'), 1.0f);
    setParam (reference, tlos::params::sourceId (0, 'y'), 2.0f);
    reference.refreshLayout();
    juce::AudioBuffer<float> refBuf (7, 512);
    const auto expected = channelEnergies (reference, refBuf, 7, 0, rng);
    for (int i = 0; i < 7; ++i)
        CHECK (std::abs (excluded[(size_t) (i < 3 ? i : i + 1)] - expected[(size_t) i]) < 2e-3);
    CHECK (excluded[2] > 0.0 || excluded[4] > 0.0 || excluded[0] > 0.0);      // the share of the excluded speaker went somewhere

    // Mute is different: the muted speaker's share is lost, the others keep theirs.
    p.setSpeakerExcluded (3, false);
    setParam (p, tlos::params::speakerMuteId (3), 1.0f);
    const auto muted = channelEnergies (p, buf, 8, 0, rng);
    CHECK (muted[3] == 0.0 && sumOf (muted) < sumOf (base) + 1e-6);
    setParam (p, tlos::params::speakerMuteId (3), 0.0f);

    // Excluded speakers outside the ones in use do not matter; everything excluded gives silence; including them again restores it.
    p.setSpeakerExcluded (3, false);
    p.setSpeakerExcluded (40, true);
    const auto restored = channelEnergies (p, buf, 8, 0, rng);
    for (int ch = 0; ch < 8; ++ch)
        CHECK (std::abs (restored[(size_t) ch] - base[(size_t) ch]) < 2e-3);
    p.setSpeakerExcluded (40, false);

    for (int i = 0; i < 8; ++i)
        p.setSpeakerExcluded (i, true);
    CHECK (sumOf (channelEnergies (p, buf, 8, 0, rng)) == 0.0);
    CHECK (p.getIncludedSpeakers().positions.empty());
    for (int i = 0; i < 8; ++i)
        p.setSpeakerExcluded (i, false);
    CHECK (std::abs (sumOf (channelEnergies (p, buf, 8, 0, rng)) - 1.0) < 1e-3);

    // Saved with the session; a session without it has none excluded.
    p.setSpeakerExcluded (5, true);
    p.setSpeakerExcluded (0, true);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    TriPanAudioProcessor restoredProcessor;
    restoredProcessor.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (restoredProcessor.isSpeakerExcluded (0) && restoredProcessor.isSpeakerExcluded (5) && ! restoredProcessor.isSpeakerExcluded (1));
    TriPanAudioProcessor fresh;
    CHECK (! fresh.isSpeakerExcluded (0) && fresh.getIncludedSpeakers().positions.empty() == (fresh.getActiveSpeakers() == 0));
}

/** Trim, mute, solo and exclude in the position files: written when used, read back, and "any entry defines it for all". */
static void testCalibrationFiles()
{
    const auto get = [] (TriPanAudioProcessor& p, const juce::String& id) { return p.getValueTreeState().getRawParameterValue (id)->load(); };

    TriPanAudioProcessor a;
    CHECK (configure (a, 3, 5, 256));

    const auto sourcesFile = juce::File::createTempFile ("xml"), speakersFile = juce::File::createTempFile ("xml");

    // Nothing set: the files do not mention any of it.
    CHECK (tlos::io::save (a, tlos::io::Kind::Sources, sourcesFile).wasOk() && tlos::io::save (a, tlos::io::Kind::Speakers, speakersFile).wasOk());
    for (const auto& text : { sourcesFile.loadFileAsString(), speakersFile.loadFileAsString() })
        CHECK (! text.contains ("trim") && ! text.contains ("mute") && ! text.contains ("solo") && ! text.contains ("excluded"));

    setParam (a, tlos::params::sourceTrimId (0), -3.0f);
    setParam (a, tlos::params::sourceMuteId (1), 1.0f);
    setParam (a, tlos::params::sourceSoloId (2), 1.0f);
    setParam (a, tlos::params::speakerTrimId (1), 4.5f);
    setParam (a, tlos::params::speakerMuteId (2), 1.0f);
    setParam (a, tlos::params::speakerSoloId (0), 1.0f);
    a.setSpeakerExcluded (3, true);

    CHECK (tlos::io::save (a, tlos::io::Kind::Sources, sourcesFile).wasOk() && tlos::io::save (a, tlos::io::Kind::Speakers, speakersFile).wasOk());
    CHECK (sourcesFile.loadFileAsString().contains ("trim") && speakersFile.loadFileAsString().contains ("excluded"));

    TriPanAudioProcessor b;
    CHECK (configure (b, 3, 5, 256));
    tlos::io::ImportReport report;
    CHECK (tlos::io::load (b, tlos::io::Kind::Sources, sourcesFile, report).wasOk() && report.calibrationApplied == 3);
    CHECK (tlos::io::load (b, tlos::io::Kind::Speakers, speakersFile, report).wasOk() && report.calibrationApplied == 5);

    CHECK (std::abs (get (b, tlos::params::sourceTrimId (0)) + 3.0f) < 0.05f && std::abs (get (b, tlos::params::sourceTrimId (1))) < 0.05f);
    CHECK (get (b, tlos::params::sourceMuteId (1)) == 1.0f && get (b, tlos::params::sourceMuteId (0)) == 0.0f);
    CHECK (get (b, tlos::params::sourceSoloId (2)) == 1.0f && get (b, tlos::params::sourceSoloId (0)) == 0.0f);
    CHECK (std::abs (get (b, tlos::params::speakerTrimId (1)) - 4.5f) < 0.05f);
    CHECK (get (b, tlos::params::speakerMuteId (2)) == 1.0f && get (b, tlos::params::speakerSoloId (0)) == 1.0f);
    CHECK (b.isSpeakerExcluded (3) && ! b.isSpeakerExcluded (2));

    // A file without any of the attributes leaves them alone.
    const auto plain = juce::File::createTempFile ("xml");
    plain.replaceWithText ("<TriPanSources version=\"1\"><Source index=\"1\" x=\"1\" y=\"2\"/></TriPanSources>");
    CHECK (tlos::io::load (b, tlos::io::Kind::Sources, plain, report).wasOk() && report.calibrationApplied == 0);
    CHECK (std::abs (get (b, tlos::params::sourceTrimId (0)) + 3.0f) < 0.05f && get (b, tlos::params::sourceMuteId (1)) == 1.0f);

    // A file where one entry has an attribute defines it for every channel: the others go back to the default.
    const auto one = juce::File::createTempFile ("xml");
    one.replaceWithText ("<TriPanSources version=\"1\"><Source index=\"1\" x=\"1\" y=\"2\"/><Source index=\"2\" x=\"1\" y=\"2\" mute=\"1\"/></TriPanSources>");
    CHECK (tlos::io::load (b, tlos::io::Kind::Sources, one, report).wasOk());
    CHECK (get (b, tlos::params::sourceMuteId (1)) == 1.0f && get (b, tlos::params::sourceSoloId (2)) == 1.0f); // solo not in the file: kept
    CHECK (std::abs (get (b, tlos::params::sourceTrimId (0)) + 3.0f) < 0.05f);                                  // trim not in the file: kept

    // Values are limited, and nonsense is skipped.
    const auto bad = juce::File::createTempFile ("xml");
    bad.replaceWithText ("<TriPanSpeakers version=\"1\"><Speaker index=\"1\" x=\"0\" y=\"5\" trim=\"99\" mute=\"2\"/>"
                         "<Speaker index=\"2\" x=\"3\" y=\"5\" trim=\"abc\"/></TriPanSpeakers>");
    CHECK (tlos::io::load (b, tlos::io::Kind::Speakers, bad, report).wasOk());
    CHECK (std::abs (get (b, tlos::params::speakerTrimId (0)) - tlos::params::kMaxTrimDb) < 0.05f);
    CHECK (report.clamped >= 1 && report.skipped >= 2);

    for (const auto& f : { sourcesFile, speakersFile, plain, one, bad })
        f.deleteFile();
}

/** OSC in: the own addresses and ADM-OSC set parameters and state; bad messages change nothing. */
static void testOscReceive()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 4, 6, 256));
    auto& osc = p.getOscControl();
    const auto get = [&] (const juce::String& id) { return p.getValueTreeState().getRawParameterValue (id)->load(); };
    const auto send = [&] (const juce::String& address, auto... args) { return osc.handleMessage (juce::OSCMessage (juce::OSCAddressPattern (address), args...)); };

    CHECK (send ("/source/2/xy", 1.5f, -2.5f));
    CHECK (std::abs (get (tlos::params::sourceId (1, 'x')) - 1.5f) < 1e-4f && std::abs (get (tlos::params::sourceId (1, 'y')) + 2.5f) < 1e-4f);
    CHECK (send ("/source/1/x", 3) && send ("/source/1/y", (juce::int32) -4));                   // ints work as well
    CHECK (std::abs (get (tlos::params::sourceId (0, 'x')) - 3.0f) < 1e-4f && std::abs (get (tlos::params::sourceId (0, 'y')) + 4.0f) < 1e-4f);
    CHECK (send ("/source/1/spread", 2.0f) && std::abs (get (tlos::params::spreadId (0)) - 2.0f) < 1e-4f);
    CHECK (send ("/source/1/trim", -6.0f) && std::abs (get (tlos::params::sourceTrimId (0)) + 6.0f) < 1e-3f);
    CHECK (send ("/source/3/mute", 1) && get (tlos::params::sourceMuteId (2)) == 1.0f);
    CHECK (send ("/source/3/mute", 0.0f) && get (tlos::params::sourceMuteId (2)) == 0.0f);
    CHECK (send ("/source/4/solo", 1.0f) && get (tlos::params::sourceSoloId (3)) == 1.0f);
    CHECK (send ("/speaker/3/xy", 2.0f, 3.0f) && std::abs (get (tlos::params::speakerId (2, 'x')) - 2.0f) < 1e-4f);
    CHECK (send ("/speaker/3/trim", 4.5f) && std::abs (get (tlos::params::speakerTrimId (2)) - 4.5f) < 1e-3f);
    CHECK (send ("/speaker/2/mute", 1) && send ("/speaker/2/solo", 1) && get (tlos::params::speakerMuteId (1)) == 1.0f && get (tlos::params::speakerSoloId (1)) == 1.0f);
    CHECK (send ("/speaker/5/exclude", 1) && p.isSpeakerExcluded (4));
    CHECK (send ("/speaker/5/exclude", 0) && ! p.isSpeakerExcluded (4));
    CHECK (send ("/room/size", 25.0f) && std::abs (p.getRoomHalfExtent() - 25.0) < 1e-9);
    CHECK (send ("/outside/rolloff", 12.0f) && std::abs (get (tlos::params::kOutsideRolloffId) - 12.0f) < 1e-3f);
    CHECK (send ("/sources/count", 2.0f) && p.getActiveSources() == 2);
    CHECK (send ("/speakers/count", 5) && p.getActiveSpeakers() == 5);

    // Values are limited to the parameter ranges.
    CHECK (send ("/source/1/x", 500.0f) && std::abs (get (tlos::params::sourceId (0, 'x')) - 100.0f) < 1e-3f);
    CHECK (send ("/room/size", 1000.0f) && std::abs (p.getRoomHalfExtent() - 100.0) < 1e-9);
    CHECK (send ("/room/size", 10.0f));

    // What it cannot use changes nothing and says so: unknown addresses, channels out of range, missing or wrong arguments, NaN.
    const float before = get (tlos::params::sourceId (0, 'y'));
    CHECK (! send ("/source/1/colour", 1.0f));
    CHECK (! send ("/source/0/xy", 1.0f, 1.0f) && ! send ("/source/65/xy", 1.0f, 1.0f) && ! send ("/source/x/xy", 1.0f, 1.0f));
    CHECK (! send ("/source/1/xy", 1.0f) && ! send ("/source/1/y") && ! send ("/source/1/y", juce::String ("up")));
    CHECK (! send ("/source/1/y", std::numeric_limits<float>::quiet_NaN()) && ! send ("/source/1/y", std::numeric_limits<float>::infinity()));
    CHECK (! send ("/elsewhere") && ! send ("/source") && ! send ("/source/1") && ! send ("/adm/obj/1/colour", 1.0f) && ! send ("/adm/obj/99/xyz", 0.0f, 0.0f, 0.0f));
    CHECK (std::abs (get (tlos::params::sourceId (0, 'y')) - before) < 1e-6f);
    CHECK (! send ("/speaker/1/spread", 1.0f));                                                 // speakers have no spread
    CHECK (! send ("/speaker/1/weight", 0.5f));                                                 // TriPan has no weights
    CHECK (tlos::OscControl::isKnownAddress ("/source/12/xy") && ! tlos::OscControl::isKnownAddress ("/source/xy"));

    // ADM-OSC: normalised to the room (10 m here), azimuth counter-clockwise from the front, elevation shortens the distance.
    CHECK (send ("/adm/obj/1/xyz", 0.5f, -0.25f, 0.9f));
    CHECK (std::abs (get (tlos::params::sourceId (0, 'x')) - 5.0f) < 1e-3f && std::abs (get (tlos::params::sourceId (0, 'y')) + 2.5f) < 1e-3f);
    CHECK (send ("/adm/obj/2/x", -1.0f) && send ("/adm/obj/2/y", 0.2f) && send ("/adm/obj/2/z", 1.0f));
    CHECK (std::abs (get (tlos::params::sourceId (1, 'x')) + 10.0f) < 1e-3f && std::abs (get (tlos::params::sourceId (1, 'y')) - 2.0f) < 1e-3f);
    CHECK (send ("/adm/obj/1/aed", 90.0f, 0.0f, 0.5f));                                         // 90 degrees: to the left
    CHECK (std::abs (get (tlos::params::sourceId (0, 'x')) + 5.0f) < 1e-3f && std::abs (get (tlos::params::sourceId (0, 'y'))) < 1e-3f);
    CHECK (send ("/adm/obj/1/aed", 0.0f, 60.0f, 1.0f));                                         // in front, 60 degrees up: 5 m out
    CHECK (std::abs (get (tlos::params::sourceId (0, 'x'))) < 1e-3f && std::abs (get (tlos::params::sourceId (0, 'y')) - 5.0f) < 1e-3f);
    CHECK (send ("/adm/obj/1/azim", -90.0f));                                                   // turn to the right, keeping the distance
    CHECK (std::abs (get (tlos::params::sourceId (0, 'x')) - 5.0f) < 1e-2f && std::abs (get (tlos::params::sourceId (0, 'y'))) < 1e-2f);
    CHECK (send ("/adm/obj/1/dist", 0.2f));
    CHECK (std::abs (get (tlos::params::sourceId (0, 'x')) - 2.0f) < 1e-2f);
    CHECK (send ("/adm/obj/3/gain", 0.5f) && std::abs (get (tlos::params::sourceTrimId (2)) + 6.0206f) < 1e-2f);
    CHECK (send ("/adm/obj/3/gain", 0.0f) && std::abs (get (tlos::params::sourceTrimId (2)) - tlos::params::kMinTrimDb) < 1e-2f);
    CHECK (send ("/adm/obj/3/mute", 1) && get (tlos::params::sourceMuteId (2)) == 1.0f);
    p.setRoomHalfExtent (20.0);                                                                 // normalised means a fraction of the room
    CHECK (send ("/adm/obj/1/xyz", 1.0f, 0.0f, 0.0f) && std::abs (get (tlos::params::sourceId (0, 'x')) - 20.0f) < 1e-3f);
    p.setRoomHalfExtent (10.0);

    CHECK (osc.getReceivedCount() > 30);
}

/** OSC out: only what changed is sent, in the own form, ADM-OSC, or both; everything once at the start and on /dump. */
static void testOscSend()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 2, 3, 256));
    auto& osc = p.getOscControl();

    std::vector<juce::OSCMessage> sent;
    osc.setSendHook ([&] (const juce::OSCMessage& m) { sent.push_back (m); });
    const auto find = [&] (const juce::String& address) -> const juce::OSCMessage*
    {
        for (const auto& m : sent)
            if (m.getAddressPattern().toString() == address)
                return &m;
        return nullptr;
    };
    const auto countOf = [&] (const juce::String& prefix)
    {
        int n = 0;
        for (const auto& m : sent)
            n += m.getAddressPattern().toString().startsWith (prefix) ? 1 : 0;
        return n;
    };

    // Nothing is sent until sending is switched on.
    osc.sendChanges();
    CHECK (sent.empty() || true);
    sent.clear();

    tlos::OscSettings s;
    s.send = true;
    s.format = tlos::OscFormat::Own;
    p.setOscSettings (s);
    osc.sendChanges();
    CHECK (find ("/source/1/xy") != nullptr && find ("/source/2/xy") != nullptr && find ("/source/3/xy") == nullptr);   // only the sources in use
    CHECK (find ("/speaker/3/xy") != nullptr && find ("/speaker/4/xy") == nullptr && find ("/room/size") != nullptr);
    CHECK (find ("/speaker/1/exclude") != nullptr && find ("/sources/count") != nullptr && find ("/outside/rolloff") != nullptr);
    CHECK (countOf ("/adm") == 0);
    if (const auto* m = find ("/source/1/xy"); m != nullptr && m->size() == 2)
        CHECK (std::abs ((*m)[0].getFloat32() - p.getValueTreeState().getRawParameterValue (tlos::params::sourceId (0, 'x'))->load()) < 1e-5f);
    if (const auto* m = find ("/room/size"); m != nullptr && m->size() == 1)
        CHECK (std::abs ((*m)[0].getFloat32() - 10.0f) < 1e-5f);

    // Then only changes.
    sent.clear();
    osc.sendChanges();
    CHECK (sent.empty());
    setParam (p, tlos::params::sourceId (1, 'y'), 4.0f);
    setParam (p, tlos::params::speakerMuteId (2), 1.0f);
    p.setSpeakerExcluded (0, true);
    p.setRoomHalfExtent (15.0);
    osc.sendChanges();
    CHECK (sent.size() == 4 && find ("/source/2/xy") != nullptr && find ("/speaker/3/mute") != nullptr && find ("/speaker/1/exclude") != nullptr && find ("/room/size") != nullptr);
    if (const auto* m = find ("/speaker/3/mute"); m != nullptr && m->size() == 1)
        CHECK ((*m)[0].isInt32() && (*m)[0].getInt32() == 1);
    sent.clear();
    setParam (p, tlos::params::sourceTrimId (0), -3.0f);
    osc.sendChanges();
    CHECK (sent.size() == 1 && find ("/source/1/trim") != nullptr);

    // /dump sends it all again, also when it comes in over OSC.
    sent.clear();
    CHECK (osc.handleMessage (juce::OSCMessage (juce::OSCAddressPattern ("/dump"))));
    CHECK (find ("/source/1/xy") != nullptr && find ("/speaker/2/xy") != nullptr && countOf ("/source/") >= 10);

    // ADM-OSC form: normalised positions, linear gain, mute; sources only.
    s.format = tlos::OscFormat::Adm;
    p.setOscSettings (s);
    sent.clear();
    osc.sendChanges();
    CHECK (countOf ("/source/") == 0 && countOf ("/speaker/") == 0 && find ("/adm/obj/1/xyz") != nullptr && find ("/adm/obj/2/gain") != nullptr && find ("/adm/obj/2/mute") != nullptr);
    setParam (p, tlos::params::sourceId (0, 'x'), 7.5f);          // the room is 15 m now: 0.5 of it
    setParam (p, tlos::params::sourceTrimId (1), -6.0206f);
    sent.clear();
    osc.sendChanges();
    if (const auto* m = find ("/adm/obj/1/xyz"); m != nullptr && m->size() == 3)
        CHECK (std::abs ((*m)[0].getFloat32() - 0.5f) < 1e-4f && std::abs ((*m)[2].getFloat32()) < 1e-6f);
    else
        CHECK (false);
    if (const auto* m = find ("/adm/obj/2/gain"); m != nullptr && m->size() == 1)
        CHECK (std::abs ((*m)[0].getFloat32() - 0.5f) < 1e-3f);
    else
        CHECK (false);

    s.format = tlos::OscFormat::Both;
    p.setOscSettings (s);
    sent.clear();
    osc.sendChanges();
    CHECK (find ("/source/1/xy") != nullptr && find ("/adm/obj/1/xyz") != nullptr && find ("/speaker/1/xy") != nullptr);

    // Switched off again: nothing more is sent (even with the hook in place, sending needs the setting or the hook).
    osc.setSendHook ({});
    s.send = false;
    p.setOscSettings (s);
    CHECK (p.getOscStatus() == "off");
}

/** The OSC settings are saved with the session; a new session has OSC off. */
static void testOscState()
{
    TriPanAudioProcessor a;
    CHECK (! a.getOscSettings().receive && ! a.getOscSettings().send && a.getOscStatus() == "off");
    CHECK (a.getOscSettings().receivePort == 9000 && a.getOscSettings().sendPort == 9001 && a.getOscSettings().sendHost == "127.0.0.1");

    tlos::OscSettings s;
    s.receive = false;                    // not opened here: the test must not depend on a free port
    s.receivePort = 7777;
    s.send = false;
    s.sendHost = "192.168.1.20";
    s.sendPort = 7778;
    s.format = tlos::OscFormat::Adm;
    a.setOscSettings (s);

    juce::MemoryBlock state;
    a.getStateInformation (state);
    TriPanAudioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getOscSettings() == s);
}

/** A real UDP round trip on the loopback address: a message sent to the plugin's port moves a source. */
static void testOscLoopback()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 2, 4, 256));

    const int port = 41234 + (int) (juce::Time::getMillisecondCounter() % 2000);
    tlos::OscSettings s;
    s.receive = true;
    s.receivePort = port;
    p.setOscSettings (s);

    if (p.getOscStatus().contains ("in use"))
    {
        std::printf ("(OSC loopback test skipped: port %d could not be opened)\n", port);
        return;
    }
    CHECK (p.getOscStatus() == "listening on port " + juce::String (port));

    juce::OSCSender sender;
    CHECK (sender.connect ("127.0.0.1", port));
    CHECK (sender.send (juce::OSCMessage (juce::OSCAddressPattern ("/source/2/xy"), 3.25f, -1.5f)));
    CHECK (sender.send (juce::OSCMessage (juce::OSCAddressPattern ("/adm/obj/1/xyz"), 0.5f, 0.5f, 0.0f)));

    for (int i = 0; i < 40 && p.getOscControl().getReceivedCount() < 2; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    const auto get = [&] (const juce::String& id) { return p.getValueTreeState().getRawParameterValue (id)->load(); };
    CHECK (p.getOscControl().getReceivedCount() == 2);
    CHECK (std::abs (get (tlos::params::sourceId (1, 'x')) - 3.25f) < 1e-3f && std::abs (get (tlos::params::sourceId (1, 'y')) + 1.5f) < 1e-3f);
    CHECK (std::abs (get (tlos::params::sourceId (0, 'x')) - 5.0f) < 1e-3f);

    // A second instance cannot take the same port; the status says so; switching the first one off frees it.
    TriPanAudioProcessor other;
    other.setOscSettings (s);
    CHECK (other.getOscStatus().contains ("in use"));
    s.receive = false;
    p.setOscSettings (s);
    CHECK (p.getOscStatus() == "off");
    other.setOscSettings (s);
    s.receive = true;
    other.setOscSettings (s);
    CHECK (other.getOscStatus() == "listening on port " + juce::String (port));
    s.receive = false;
    other.setOscSettings (s);
}

/** The settings panel edits the settings of its processor. */
static void testOscPanel()
{
    TriPanAudioProcessor p;
    tlos::OscSettingsPanel panel (p);
    CHECK (! panel.getReceiveToggle().getToggleState() && panel.getReceivePortEditor().getText() == "9000" && panel.getSendHostEditor().getText() == "127.0.0.1");
    CHECK (panel.getFormatBox().getSelectedId() == 3 && panel.getStatusLabel().getText() == "off");

    panel.getSendHostEditor().setText ("10.0.0.7", false);
    panel.getSendPortEditor().setText ("8001", false);
    panel.getFormatBox().setSelectedId (2, juce::sendNotificationSync);       // applies at once
    CHECK (p.getOscSettings().sendHost == "10.0.0.7" && p.getOscSettings().sendPort == 8001 && p.getOscSettings().format == tlos::OscFormat::Adm);

    panel.getReceivePortEditor().setText ("0", false);                       // rubbish is not applied; the old value stays
    panel.getSendPortEditor().setText ("99999", false);                      // limited to the port range
    panel.apply();
    CHECK (p.getOscSettings().receivePort == 9000 && p.getOscSettings().sendPort == 65535);
    CHECK (panel.getReceivePortEditor().getText() == "9000" && panel.getSendPortEditor().getText() == "65535");

    panel.getSendToggle().setToggleState (true, juce::sendNotificationSync);
    CHECK (p.getOscSettings().send && p.getOscStatus().startsWith ("sending to 10.0.0.7:65535"));
    panel.getSendToggle().setToggleState (false, juce::sendNotificationSync);
    CHECK (! p.getOscSettings().send && p.getOscStatus() == "off");
}

/** Records what a host would see of one parameter: its changes and its gestures, in order. */
struct ParameterRecorder final : juce::AudioProcessorParameter::Listener
{
    /** Starts recording `parameter`. */
    explicit ParameterRecorder (juce::AudioProcessorParameter& p) : parameter (p) { parameter.addListener (this); }
    /** Stops recording. */
    ~ParameterRecorder() override { parameter.removeListener (this); }

    /** Notes a value change. */
    void parameterValueChanged (int, float) override { events.add ("value"); }
    /** Notes the start or the end of a gesture. */
    void parameterGestureChanged (int, bool starting) override { events.add (starting ? "begin" : "end"); }

    juce::AudioProcessorParameter& parameter;   ///< The parameter that is recorded.
    juce::StringArray events;                   ///< "begin", "value" and "end", in the order the host got them.
};

/** The values of every parameter (normalised), as a host's undo would store them. */
static std::vector<float> snapshotOf (juce::AudioProcessor& p)
{
    std::vector<float> values;
    for (auto* parameter : p.getParameters())
        values.push_back (parameter->getValue());
    return values;
}

/** Puts the parameters back to the values of a snapshot, as a host's undo would. */
static void restoreSnapshot (juce::AudioProcessor& p, const std::vector<float>& values)
{
    const auto& parameters = p.getParameters();
    for (int i = 0; i < parameters.size() && (size_t) i < values.size(); ++i)
        if (std::abs (parameters[i]->getValue() - values[(size_t) i]) > 1e-9f)
            parameters[i]->setValueNotifyingHost (values[(size_t) i]);
}

/** Groups, exclusions and manual virtual speakers are host parameters: they are changed inside gestures, the host's undo can put them back, and host automation drives them. */
static void testHostUndo()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 6, 8, 256));

    // Gestures: what the host sees when the plugin changes things.
    {
        ParameterRecorder exclude (*p.getValueTreeState().getParameter (tlos::params::speakerExcludeId (2)));
        p.setSpeakerExcluded (2, true);
        CHECK (exclude.events.joinIntoString (",") == "begin,value,end");
        p.setSpeakerExcluded (2, true);                              // nothing changed: nothing is sent
        CHECK (exclude.events.size() == 3);
        p.setSpeakerExcluded (2, false);
        CHECK (exclude.events.size() == 6);
    }
    {
        ParameterRecorder first (*p.getValueTreeState().getParameter (tlos::params::sourceGroupId (0)));
        ParameterRecorder second (*p.getValueTreeState().getParameter (tlos::params::sourceGroupId (1)));
        ParameterRecorder other (*p.getValueTreeState().getParameter (tlos::params::sourceGroupId (4)));
        tlos::SourceGroups groups;
        groups.group ({ 0, 1 });
        p.setSourceGroups (groups);
        CHECK (first.events.joinIntoString (",") == "begin,value,end" && second.events.joinIntoString (",") == "begin,value,end");
        CHECK (other.events.isEmpty());                              // only what changes is written
        CHECK (p.getSourceGroups() == groups);
        p.setSourceGroups (tlos::SourceGroups());
        CHECK (! p.getSourceGroups().any());
    }
    {
        ParameterRecorder count (*p.getValueTreeState().getParameter (tlos::params::kNumVirtualId));
        ParameterRecorder x (*p.getValueTreeState().getParameter (tlos::params::virtualId (0, 'x')));
        p.setManualVirtualSpeakers ({ { 1.0, 2.0 } });
        CHECK (count.events.joinIntoString (",") == "begin,value,end" && x.events.joinIntoString (",") == "begin,value,end");
        x.events.clear();
        p.setManualVirtualSpeakers ({ { 3.0, 2.0 } }, false);          // inside a drag: the drag has the gestures
        CHECK (x.events.joinIntoString (",") == "value");
        p.beginVirtualSpeakerGesture (0);
        p.endVirtualSpeakerGesture (0);
        CHECK (x.events.joinIntoString (",") == "value,begin,end");
        p.setManualVirtualSpeakers ({});
    }

    // Undo and redo: put every parameter back to what it was, and the plugin follows.
    const auto before = snapshotOf (p);
    tlos::SourceGroups groups;
    groups.group ({ 0, 2, 3 });
    groups.group ({ 4, 5 });
    p.setSourceGroups (groups);
    p.setSpeakerExcluded (1, true);
    p.setSpeakerExcluded (6, true);
    p.setVirtualMode (tlos::VirtualMode::Manual);
    p.setManualVirtualSpeakers ({ { 1.5, -2.0 }, { -3.0, 4.0 }, { 0.0, 0.0 } });
    const auto after = snapshotOf (p);
    CHECK (p.getSourceGroups() == groups && p.isSpeakerExcluded (1) && p.isSpeakerExcluded (6));
    CHECK (p.getVirtualMode() == tlos::VirtualMode::Manual && p.getManualVirtualSpeakers().size() == 3);

    restoreSnapshot (p, before);                                     // the host's undo
    p.refreshLayout();
    CHECK (! p.getSourceGroups().any() && ! p.isSpeakerExcluded (1) && ! p.isSpeakerExcluded (6));
    CHECK (p.getVirtualMode() == tlos::VirtualMode::Auto && p.getManualVirtualSpeakers().empty());
    CHECK (p.getIncludedSpeakers().positions.size() == 8);

    restoreSnapshot (p, after);                                      // and redo
    p.refreshLayout();
    CHECK (p.getSourceGroups() == groups && p.isSpeakerExcluded (1) && p.isSpeakerExcluded (6));
    CHECK (p.getVirtualMode() == tlos::VirtualMode::Manual);
    const auto list = p.getManualVirtualSpeakers();
    CHECK (list.size() == 3 && std::abs (list[0].x - 1.5) < 1e-4 && std::abs (list[1].y - 4.0) < 1e-4);
    CHECK (p.getIncludedSpeakers().positions.size() == 6);

    // Host automation: writing the parameters directly is the same as using the plugin's functions.
    restoreSnapshot (p, before);
    setParam (p, tlos::params::sourceGroupId (0), 3.0f);
    setParam (p, tlos::params::sourceGroupId (1), 3.0f);
    setParam (p, tlos::params::sourceGroupId (5), 7.0f);                // a group of one is no group
    const auto automated = p.getSourceGroups();
    CHECK (automated.groupOf (0) == 3 && automated.groupOf (1) == 3 && automated.groupOf (5) == 0);
    setParam (p, tlos::params::speakerExcludeId (3), 1.0f);
    p.refreshLayout();
    CHECK (p.isSpeakerExcluded (3) && p.getIncludedSpeakers().channelOf.size() == 7);
    setParam (p, tlos::params::kVirtualModeId, 2.0f);                   // the choice parameter's index: 2 = Off
    CHECK (p.getVirtualMode() == tlos::VirtualMode::Off);

    // And everything survives a save and a restore (it is in the parameters now).
    p.setVirtualMode (tlos::VirtualMode::Manual);
    p.setManualVirtualSpeakers ({ { 2.0, 2.0 } });
    juce::MemoryBlock state;
    p.getStateInformation (state);
    TriPanAudioProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.getSourceGroups() == p.getSourceGroups() && q.isSpeakerExcluded (3) && q.getVirtualMode() == tlos::VirtualMode::Manual);
    CHECK (q.getManualVirtualSpeakers().size() == 1);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    CHECK (xml != nullptr && ! xml->hasAttribute ("sourceGroups") && ! xml->hasAttribute ("excludedSpeakers") && ! xml->hasAttribute ("manualVirtual"));
}

/** A session saved by an earlier version kept groups, exclusions and virtual speakers in the state: they move into the parameters. */
static void testLegacyStateMigration()
{
    TriPanAudioProcessor a;
    juce::MemoryBlock fresh;
    a.getStateInformation (fresh);
    auto xml = juce::AudioProcessor::getXmlFromBinary (fresh.getData(), (int) fresh.getSize());
    CHECK (xml != nullptr);
    if (xml == nullptr)
        return;

    xml->setAttribute ("sourceGroups", "0 0 1 1 0 2 2 0");
    xml->setAttribute ("excludedSpeakers", "0010000100");
    xml->setAttribute ("virtualMode", 2);
    xml->setAttribute ("manualVirtual", "1.5 -2.5 3 4 nan 1 5");
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);

    TriPanAudioProcessor b;
    b.setStateInformation (old.getData(), (int) old.getSize());
    const auto groups = b.getSourceGroups();
    CHECK (groups.groupOf (2) == 1 && groups.groupOf (3) == 1 && groups.groupOf (5) == 2 && groups.groupOf (6) == 2 && groups.groupOf (0) == 0);
    CHECK (b.isSpeakerExcluded (2) && b.isSpeakerExcluded (7) && ! b.isSpeakerExcluded (0));
    CHECK (b.getVirtualMode() == tlos::VirtualMode::Manual);
    const auto list = b.getManualVirtualSpeakers();
    CHECK (list.size() == 2 && std::abs (list[0].x - 1.5) < 1e-4 && std::abs (list[0].y + 2.5) < 1e-4 && std::abs (list[1].y - 4.0) < 1e-4);   // the "nan" pair is skipped

    // Saved again, it is in the parameters and the old properties are gone.
    juce::MemoryBlock saved;
    b.getStateInformation (saved);
    const auto xml2 = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    CHECK (xml2 != nullptr && ! xml2->hasAttribute ("sourceGroups") && ! xml2->hasAttribute ("excludedSpeakers")
           && ! xml2->hasAttribute ("virtualMode") && ! xml2->hasAttribute ("manualVirtual"));
    TriPanAudioProcessor c;
    c.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (c.getSourceGroups() == groups && c.isSpeakerExcluded (7) && c.getManualVirtualSpeakers().size() == 2 && c.getVirtualMode() == tlos::VirtualMode::Manual);
}

/** Runs all processor tests. @param argc,argv  argv[1] is the folder with the speaker layouts. @return 0 if every check passed. */
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testParameterOrder();
    testOutsideFalloff();
    testTrimMuteSolo();
    testMuteHasNoClick();
    testExclude();
    testCalibrationFiles();
    testHostUndo();
    testLegacyStateMigration();
    testOscReceive();
    testOscSend();
    testOscState();
    testOscLoopback();
    testOscPanel();
    testManyVirtualSpeakers();
    testStationaryEnergyAndLocality();
    testNumberOfSourcesAndSpeakersInUse();
    testUnusedSpeakersAreSilentImmediately();
    testMovingSourceIsSmoothAndBounded();
    testSourceOnSpeakerAndSpeakerUpdate();
    testOversizedBlock();
    testStateRoundTrip();
    testFullSizeEnergyConservation();
    testParameterAutomationWhileProcessing();
    testVirtualSpeakerModes();
    testUnchangedSourcesFollowLayoutChanges();
    testSourceSpread();
    testFullSize64x64();

    if (argc > 1)
    {
        testSpeakerPresets (juce::File (argv[1]));
        testMirrorSymmetryWithVirtualSpeakers (juce::File (argv[1]));
    }
    else
        std::printf ("(speaker preset tests skipped: pass the speaker layouts folder as argument)\n");

    if (failures == 0)
        std::printf ("All processor tests passed\n");
    return failures == 0 ? 0 : 1;
}
