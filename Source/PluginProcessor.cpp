#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "tripan/Limits.h"

TriPanAudioProcessor::TriPanAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Sources",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Speakers", juce::AudioChannelSet::stereo(), true))
{
}

void TriPanAudioProcessor::prepareToPlay (double, int)
{
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

    // M1: pass-through. Channel i goes to channel i; extra outputs are silenced.
    // (Channels beyond the input count are already cleared below; extra inputs are ignored.)
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* TriPanAudioProcessor::createEditor()
{
    return new TriPanAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TriPanAudioProcessor();
}
