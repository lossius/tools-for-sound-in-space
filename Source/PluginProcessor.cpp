#include "PluginProcessor.h"
#include "Parameters.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
const juce::Identifier kLayoutInitialised { "layoutInitialised" };

/** k-th of `count` positions on a ring; k = 0 at the front (+y), going clockwise. */
juce::Point<float> ringPosition (int k, int count, float radius)
{
    const float theta = count == 2 ? (k == 0 ? -juce::MathConstants<float>::pi / 4.0f : juce::MathConstants<float>::pi / 4.0f)
                                   : juce::MathConstants<float>::twoPi * (float) k / (float) std::max (count, 1);
    return { radius * std::sin (theta), radius * std::cos (theta) };
}

float gainOf (const tripan::SourceGains& g, int speaker) noexcept
{
    for (int i = 0; i < g.count; ++i)
        if (g.speaker[(size_t) i] == speaker)
            return g.gain[(size_t) i];
    return 0.0f;
}

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
        spkX[(size_t) i] = apvts.getRawParameterValue (tripan::params::speakerId (i, 'x'));
        spkY[(size_t) i] = apvts.getRawParameterValue (tripan::params::speakerId (i, 'y'));
    }

    startTimerHz (30);
}

TriPanAudioProcessor::~TriPanAudioProcessor()
{
    stopTimer();
}

void TriPanAudioProcessor::setParameterValue (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

void TriPanAudioProcessor::resetLayoutToRings()
{
    const int n = std::min (getTotalNumInputChannels(),  tripan::kMaxChannels);
    const int m = std::min (getTotalNumOutputChannels(), tripan::kMaxChannels);

    for (int i = 0; i < n; ++i)
    {
        const auto p = ringPosition (i, n, 2.5f);
        setParameterValue (tripan::params::sourceId (i, 'x'), p.x);
        setParameterValue (tripan::params::sourceId (i, 'y'), p.y);
    }

    for (int i = 0; i < m; ++i)
    {
        const auto p = ringPosition (i, m, 5.0f);
        setParameterValue (tripan::params::speakerId (i, 'x'), p.x);
        setParameterValue (tripan::params::speakerId (i, 'y'), p.y);
    }

    apvts.state.setProperty (kLayoutInitialised, true, nullptr);
    refreshTriangulation();
}

void TriPanAudioProcessor::refreshTriangulation()
{
    const int m = std::min (getTotalNumOutputChannels(), tripan::kMaxChannels);

    std::vector<tripan::Vec2> speakers;
    speakers.reserve ((size_t) m);
    for (int i = 0; i < m; ++i)
        speakers.push_back ({ (double) spkX[(size_t) i]->load(), (double) spkY[(size_t) i]->load() });

    const auto same = [] (const tripan::Vec2& a, const tripan::Vec2& b) { return juce::exactlyEqual (a.x, b.x) && juce::exactlyEqual (a.y, b.y); };

    if (havePublished && speakers.size() == publishedSpeakers.size()
        && std::equal (speakers.begin(), speakers.end(), publishedSpeakers.begin(), same))
        return;

    exchange.publish (std::make_unique<const tripan::Triangulation> (speakers));
    publishedSpeakers = std::move (speakers);
    havePublished = true;
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
    const tripan::Triangulation* tri = exchange.acquire();

    std::array<tripan::SourceGains, tripan::kMaxChannels> target {};
    for (int s = 0; s < n; ++s)
        if (tri != nullptr)
            target[(size_t) s] = tripan::computeGains (*tri, { (double) srcX[(size_t) s]->load(), (double) srcY[(size_t) s]->load() });

    if (restartRamp.exchange (false))
        prevGains = target;

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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TriPanAudioProcessor();
}
