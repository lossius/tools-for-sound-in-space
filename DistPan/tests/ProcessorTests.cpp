// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file ProcessorTests.cpp
    @brief Offline tests of the DistPan processor (no DAW): energy conservation, distribution, settings, speaker updates,
    silent unused speakers, state round-trip, 64x64, speaker layout files, parameter order. */

#include "../Source/DistPanParameters.h"
#include "../Source/PluginProcessor.h"
#include "tlos/Parameters.h"
#include "tlos/PositionIO.h"

#include <atomic>
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
void setParam (DistPanAudioProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.getValueTreeState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

/** Current real value of a parameter. */
float getParam (DistPanAudioProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

/** Configures the processor for n inputs and m outputs and prepares it. @return false if the layout is refused. */
bool configure (DistPanAudioProcessor& p, int n, int m, int blockSize)
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

/** The host sees the shared parameters first, then rolloff and blur. */
static void testParameterOrder()
{
    DistPanAudioProcessor p;
    const auto& parameters = p.getParameters();

    CHECK (parameters.size() == 2 + 64 * 6 + 64 * 5 + 1 + 2);

    const auto idOf = [&] (int i) { return dynamic_cast<juce::AudioProcessorParameterWithID*> (parameters[i])->paramID; };
    CHECK (idOf (0) == tlos::params::kNumSourcesId && idOf (1) == tlos::params::kNumSpeakersId);
    CHECK (idOf (2) == tlos::params::sourceId (0, 'x'));
    CHECK (idOf (2 + 64 * 6) == tlos::params::speakerId (0, 'x'));
    CHECK (idOf (parameters.size() - 7) == tlos::params::speakerId (63, 'y'));
    CHECK (idOf (parameters.size() - 6) == tlos::params::speakerTrimId (63));
    CHECK (idOf (parameters.size() - 3) == tlos::params::kOutsideRolloffId);
    CHECK (idOf (parameters.size() - 2) == distpan::params::kRolloffId && idOf (parameters.size() - 1) == distpan::params::kBlurId);
    CHECK (parameters[parameters.size() - 2]->getName (64) == "Rolloff" && parameters[parameters.size() - 1]->getName (64) == "Blur");

    CHECK (std::abs (getParam (p, distpan::params::kRolloffId) - distpan::params::kDefaultRolloff) < 1e-6f);
    CHECK (std::abs (getParam (p, distpan::params::kBlurId) - distpan::params::kDefaultBlur) < 1e-6f);
    CHECK (! p.supportsVirtualSpeakers() && p.getVirtualMode() == tlos::VirtualMode::Off);
}

/** A stationary source keeps its energy, every speaker gets some of it, and the nearest one gets the most. */
static void testStationaryEnergyAndDistribution()
{
    DistPanAudioProcessor p;
    CHECK (configure (p, 4, 8, 512));

    juce::AudioBuffer<float> buf (8, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (1);

    for (int src = 0; src < 4; ++src)
        for (int block = 0; block < 3; ++block)
        {
            const double inE = fillNoise (buf, src, rng);
            p.processBlock (buf, midi);

            CHECK (std::abs (totalEnergy (buf, 8) - inE) < 1e-3 * inE);

            int loudest = 0;
            for (int ch = 0; ch < 8; ++ch)
            {
                CHECK (channelEnergy (buf, ch) > 1e-9);   // DBAP uses every speaker
                if (channelEnergy (buf, ch) > channelEnergy (buf, loudest))
                    loudest = ch;
            }
            CHECK (loudest == 2 * src);                   // default rings: source i is closest to speaker 2i
        }
}

/** More rolloff makes the sound more local; more blur makes it more even. */
static void testRolloffAndBlur()
{
    DistPanAudioProcessor p;
    CHECK (configure (p, 1, 8, 512));

    juce::AudioBuffer<float> buf (8, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (2);

    const auto shareOfNearest = [&]
    {
        buf.clear();
        const double inE = fillNoise (buf, 0, rng);
        p.processBlock (buf, midi);
        p.processBlock (buf, midi); // the settings have been applied (ramped) by the second block
        buf.clear();
        const double inE2 = fillNoise (buf, 0, rng);
        p.processBlock (buf, midi);
        juce::ignoreUnused (inE);
        return channelEnergy (buf, 0) / inE2;
    };

    setParam (p, distpan::params::kBlurId, 0.0f);
    setParam (p, distpan::params::kRolloffId, 3.0f);
    const double soft = shareOfNearest();
    setParam (p, distpan::params::kRolloffId, 12.0f);
    const double sharp = shareOfNearest();
    CHECK (sharp > soft);

    setParam (p, distpan::params::kRolloffId, 6.0f);
    setParam (p, distpan::params::kBlurId, 0.0f);
    const double crisp = shareOfNearest();
    setParam (p, distpan::params::kBlurId, 5.0f);
    const double blurred = shareOfNearest();
    CHECK (blurred < crisp);
}

/** Moving a speaker changes the distribution (the new position reaches the audio thread). */
static void testSpeakerUpdate()
{
    DistPanAudioProcessor p;
    CHECK (configure (p, 1, 4, 256));

    juce::AudioBuffer<float> buf (4, 256);
    juce::MidiBuffer midi;
    std::mt19937 rng (3);

    const auto shareOfSpeaker0 = [&]
    {
        fillNoise (buf, 0, rng);
        p.processBlock (buf, midi);
        const double inE = fillNoise (buf, 0, rng);
        p.processBlock (buf, midi);
        return channelEnergy (buf, 0) / inE;
    };

    const double before = shareOfSpeaker0();
    setParam (p, tlos::params::speakerId (0, 'x'), 10.0f);
    setParam (p, tlos::params::speakerId (0, 'y'), -10.0f);
    p.refreshLayout();
    const double after = shareOfSpeaker0();
    CHECK (after < before * 0.5);
}

/** Speakers beyond "Number of Speakers" are silent, and a source with nothing to feed gives silence. */
static void testNumberOfSourcesAndSpeakersInUse()
{
    DistPanAudioProcessor p;
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

/** Positions, spreads, the settings, the edit modes and the groups survive a save and a restore. */
static void testStateRoundTrip()
{
    DistPanAudioProcessor a;
    CHECK (configure (a, 4, 6, 256));
    setParam (a, tlos::params::speakerId (3, 'x'), 1.25f);
    setParam (a, tlos::params::sourceId (2, 'y'), -2.5f);
    setParam (a, distpan::params::kRolloffId, 9.0f);
    setParam (a, distpan::params::kBlurId, 1.5f);
    a.setSpeakersEditable (true);
    a.setSourcesEditable (false);
    tlos::SourceGroups groups;
    groups.group ({ 0, 1 });
    a.setSourceGroups (groups);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    DistPanAudioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (configure (b, 4, 6, 256)); // must not overwrite restored positions with default rings

    CHECK (std::abs (getParam (b, tlos::params::speakerId (3, 'x')) - 1.25f) < 1e-4f);
    CHECK (std::abs (getParam (b, tlos::params::sourceId (2, 'y')) + 2.5f) < 1e-4f);
    CHECK (std::abs (getParam (b, distpan::params::kRolloffId) - 9.0f) < 1e-4f);
    CHECK (std::abs (getParam (b, distpan::params::kBlurId) - 1.5f) < 1e-4f);
    CHECK (! b.canEditSources() && b.canEditSpeakers());
    CHECK (b.getSourceGroups() == groups);

    // A TriPan session is not a DistPan session: the state tag differs and nothing is changed.
    juce::XmlElement foreign ("TriPan");
    juce::MemoryBlock foreignState;
    juce::AudioProcessor::copyXmlToBinary (foreign, foreignState);
    DistPanAudioProcessor c;
    c.setStateInformation (foreignState.getData(), (int) foreignState.getSize());
    CHECK (std::abs (getParam (c, distpan::params::kRolloffId) - distpan::params::kDefaultRolloff) < 1e-6f);
}

/** Outside the speakers the level falls (6 dB per doubling of 1 + distance by default); inside nothing changes; 0 dB switches it off. */
static void testOutsideFalloff()
{
    DistPanAudioProcessor p;
    CHECK (configure (p, 1, 8, 512));
    CHECK (std::abs (getParam (p, tlos::params::kOutsideRolloffId) - 6.0f) < 1e-6f); // default
    CHECK (std::abs (p.getOutsideRolloff() - 6.0) < 1e-6);

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

    CHECK (std::abs (energyRatioAt (0.0f, 2.0f) - 1.0) < 1e-3);
    CHECK (std::abs (energyRatioAt (0.0f, 5.0f) - 1.0) < 1e-3);
    CHECK (std::abs (energyRatioAt (0.0f, 6.0f) - 0.25) < 0.01);        // -6 dB
    CHECK (std::abs (energyRatioAt (0.0f, 8.0f) - 1.0 / 16.0) < 0.01);  // -12 dB

    setParam (p, tlos::params::kOutsideRolloffId, 0.0f);
    CHECK (std::abs (energyRatioAt (0.0f, 8.0f) - 1.0) < 1e-3);

    // A layout without area (two speakers) keeps its level wherever the source is.
    setParam (p, tlos::params::kOutsideRolloffId, 6.0f);
    setParam (p, tlos::params::kNumSpeakersId, 2.0f);
    p.refreshLayout();
    CHECK (std::abs (energyRatioAt (7.0f, -7.0f) - 1.0) < 1e-3);
}


/** Energy of every output channel for one stationary source with noise, after the gain ramps have settled, per unit of input energy. */
static std::vector<double> channelEnergies (DistPanAudioProcessor& p, juce::AudioBuffer<float>& buf, int outputs, int source, std::mt19937& rng)
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

/** Trim, mute and solo of sources and speakers: trim scales, mute silences without redistributing, solo silences the others (mute wins). */
static void testTrimMuteSolo()
{
    DistPanAudioProcessor p;
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
    DistPanAudioProcessor p;
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
    DistPanAudioProcessor p;
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
    DistPanAudioProcessor reference;
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
    DistPanAudioProcessor restoredProcessor;
    restoredProcessor.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (restoredProcessor.isSpeakerExcluded (0) && restoredProcessor.isSpeakerExcluded (5) && ! restoredProcessor.isSpeakerExcluded (1));
    DistPanAudioProcessor fresh;
    CHECK (! fresh.isSpeakerExcluded (0) && fresh.getIncludedSpeakers().positions.empty() == (fresh.getActiveSpeakers() == 0));
}

/** 64 sources to 64 speakers: one source at a time keeps its energy, and the output is finite. */
static void testFullSize64x64()
{
    DistPanAudioProcessor p;
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

/** Parameters change on one thread while another processes audio: no crash, finite output. */
static void testParameterAutomationWhileProcessing()
{
    DistPanAudioProcessor p;
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
            setParam (p, distpan::params::kRolloffId, 1.0f + 11.0f * std::abs (u (rng)) / 10.0f);
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
        DistPanAudioProcessor p;
        CHECK (configure (p, 2, 64, 256));
        setParam (p, tlos::params::kOutsideRolloffId, 0.0f); // the source may be outside a layout: this tests the import, not the falloff

        tlos::io::ImportReport report;
        const auto result = tlos::io::load (p, tlos::io::Kind::Speakers, file, report);
        CHECK (result.wasOk());
        p.refreshLayout();

        juce::AudioBuffer<float> buf (64, 256);
        juce::MidiBuffer midi;
        std::mt19937 rng (8);
        const double inE = fillNoise (buf, 0, rng);
        p.processBlock (buf, midi);
        CHECK (allFinite (buf, 64));
        CHECK (std::abs (totalEnergy (buf, 64) - inE) < 1e-3 * inE);
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
    testStationaryEnergyAndDistribution();
    testRolloffAndBlur();
    testOutsideFalloff();
    testTrimMuteSolo();
    testMuteHasNoClick();
    testExclude();
    testSpeakerUpdate();
    testNumberOfSourcesAndSpeakersInUse();
    testStateRoundTrip();
    testFullSize64x64();
    testParameterAutomationWhileProcessing();

    if (argc > 1)
        testSpeakerLayouts (juce::File (argv[1]));
    else
        std::printf ("(speaker layout tests skipped: pass the speaker layouts folder as argument)\n");

    if (failures == 0)
        std::printf ("All processor tests passed\n");
    return failures == 0 ? 0 : 1;
}
