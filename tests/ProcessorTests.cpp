// Offline tests of the TriPan processor (no DAW): energy conservation, locality, ramping,
// speaker updates, state round-trip, large layouts.
#include "../Source/Parameters.h"
#include "../Source/PluginProcessor.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <random>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (! (cond)) {                                                      \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

namespace
{
void setParam (TriPanAudioProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.getValueTreeState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

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

double channelEnergy (const juce::AudioBuffer<float>& b, int ch)
{
    double e = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
        e += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return e;
}

bool allFinite (const juce::AudioBuffer<float>& b, int channels)
{
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}
}

static void testStationaryEnergyAndLocality()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 4, 8, 512));

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
        setParam (p, tripan::params::sourceId (0, 'x'), 6.0f * std::sin (a));
        setParam (p, tripan::params::sourceId (0, 'y'), 6.0f * std::cos (a));

        const double inE = fillNoise (buf, 2, 0, rng);
        p.processBlock (buf, midi);

        double outE = 0.0;
        for (int ch = 0; ch < 6; ++ch) outE += channelEnergy (buf, ch);
        CHECK (allFinite (buf, 6));
        CHECK (outE <= inE * 1.001);
    }
}

static void testSourceOnSpeakerAndSpeakerUpdate()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 1, 5, 256));

    juce::AudioBuffer<float> buf (5, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (3);

    // Move speaker 2 and tell the processor (the timer does this in a real host).
    setParam (p, tripan::params::speakerId (2, 'x'), -4.0f);
    setParam (p, tripan::params::speakerId (2, 'y'), -3.0f);
    p.refreshTriangulation();

    // Put the source exactly on speaker 2; after the ramp block only that output carries signal.
    setParam (p, tripan::params::sourceId (0, 'x'), -4.0f);
    setParam (p, tripan::params::sourceId (0, 'y'), -3.0f);
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

static void testStateRoundTrip()
{
    TriPanAudioProcessor a;
    CHECK (configure (a, 4, 6, 256));
    setParam (a, tripan::params::speakerId (3, 'x'), 1.25f);
    setParam (a, tripan::params::sourceId (2, 'y'), -2.5f);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    TriPanAudioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (configure (b, 4, 6, 256)); // must not overwrite restored positions with default rings

    const auto get = [] (TriPanAudioProcessor& p, const juce::String& id) { return p.getValueTreeState().getRawParameterValue (id)->load(); };
    CHECK (std::abs (get (b, tripan::params::speakerId (3, 'x')) - 1.25f) < 1e-4f);
    CHECK (std::abs (get (b, tripan::params::sourceId (2, 'y')) + 2.5f) < 1e-4f);
}

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
            setParam (p, tripan::params::sourceId (s, 'x'), 6.0f * std::sin (a));
            setParam (p, tripan::params::sourceId (s, 'y'), 6.0f * std::cos (a));
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


static void testFullSizeEnergyConservation()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 64, 64, 256));

    // Scatter the speakers irregularly (deterministically) so the triangulation is non-trivial.
    std::mt19937 rng (6);
    std::uniform_real_distribution<float> pos (-8.0f, 8.0f);
    for (int i = 0; i < 64; ++i)
    {
        setParam (p, tripan::params::speakerId (i, 'x'), pos (rng));
        setParam (p, tripan::params::speakerId (i, 'y'), pos (rng));
    }
    p.refreshTriangulation();

    juce::AudioBuffer<float> buf (64, 256);
    juce::MidiBuffer midi;

    for (int src : { 0, 17, 63 })
    {
        setParam (p, tripan::params::sourceId (src, 'x'), pos (rng));
        setParam (p, tripan::params::sourceId (src, 'y'), pos (rng));

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
        setParam (p, tripan::params::speakerId (k, 'x'), pos (rng));
        setParam (p, tripan::params::speakerId (k, 'y'), pos (rng));
        setParam (p, tripan::params::sourceId (k, 'x'), pos (rng));
        setParam (p, tripan::params::sourceId (k, 'y'), pos (rng));
        p.refreshTriangulation();
        if (iter % 50 == 0)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    done = true;
    audio.join();
    CHECK (problems.load() == 0);
}

static void testNumberOfSourcesAndSpeakersInUse()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 4, 8, 256));

    // Default: everything on the track is used; larger parameter values clamp to the channel counts.
    CHECK (p.getActiveSources() == 4 && p.getActiveSpeakers() == 8);

    juce::AudioBuffer<float> buf (8, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (9);

    // One source in use: the other sources are silent.
    setParam (p, tripan::params::kNumSourcesId, 1.0f);
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
    setParam (p, tripan::params::kNumSpeakersId, 3.0f);
    p.refreshTriangulation();
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
    setParam (p, tripan::params::kNumSpeakersId, 64.0f);
    CHECK (p.getActiveSpeakers() == 8);
}

static void testUnusedSpeakersAreSilentImmediately()
{
    TriPanAudioProcessor p;
    CHECK (configure (p, 2, 8, 256));

    juce::AudioBuffer<float> buf (8, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (10);

    // Put source 1 on speaker 7 so that speaker is definitely carrying signal.
    setParam (p, tripan::params::sourceId (0, 'x'), (float) p.getValueTreeState().getRawParameterValue (tripan::params::speakerId (6, 'x'))->load());
    setParam (p, tripan::params::sourceId (0, 'y'), (float) p.getValueTreeState().getRawParameterValue (tripan::params::speakerId (6, 'y'))->load());
    for (int block = 0; block < 3; ++block)
    {
        fillNoise (buf, 2, 0, rng);
        p.processBlock (buf, midi);
    }
    CHECK (channelEnergy (buf, 6) > 1.0);

    // Reduce to 4 speakers WITHOUT refreshing the triangulation (the audio thread still sees the
    // old one with 8 speakers, as it does for up to ~33 ms in a real host).
    setParam (p, tripan::params::kNumSpeakersId, 4.0f);

    // First block: the outputs that just became unused fade out, ending at (almost) zero.
    fillNoise (buf, 2, 0, rng);
    p.processBlock (buf, midi);
    for (int ch = 4; ch < 8; ++ch)
        CHECK (std::abs (buf.getSample (ch, 255)) < 0.02f);

    // From the next block on they are exactly silent, with or without the new triangulation.
    for (int block = 0; block < 3; ++block)
    {
        if (block == 2)
            p.refreshTriangulation();
        fillNoise (buf, 2, 0, rng);
        p.processBlock (buf, midi);
        for (int ch = 4; ch < 8; ++ch)
            CHECK (channelEnergy (buf, ch) == 0.0);
    }

    // Growing the count again lets the speakers carry signal again.
    setParam (p, tripan::params::kNumSpeakersId, 8.0f);
    p.refreshTriangulation();
    double energy = 0.0;
    for (int block = 0; block < 3; ++block)
    {
        fillNoise (buf, 2, 0, rng);
        p.processBlock (buf, midi);
        energy = channelEnergy (buf, 6);
    }
    CHECK (energy > 1.0);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testStationaryEnergyAndLocality();
    testNumberOfSourcesAndSpeakersInUse();
    testUnusedSpeakersAreSilentImmediately();
    testMovingSourceIsSmoothAndBounded();
    testSourceOnSpeakerAndSpeakerUpdate();
    testOversizedBlock();
    testStateRoundTrip();
    testFullSizeEnergyConservation();
    testParameterAutomationWhileProcessing();
    testFullSize64x64();

    if (failures == 0)
        std::printf ("All processor tests passed\n");
    return failures == 0 ? 0 : 1;
}
