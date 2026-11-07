#include "PluginEditor.h"

TriPanAudioProcessorEditor::TriPanAudioProcessorEditor (TriPanAudioProcessor& p)
    : AudioProcessorEditor (&p)
{
    setSize (600, 600);
}

void TriPanAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);
    g.setFont (20.0f);
    g.drawFittedText ("TriPan", getLocalBounds(), juce::Justification::centred, 1);
}
