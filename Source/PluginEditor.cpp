#include "PluginEditor.h"

TriPanAudioProcessorEditor::TriPanAudioProcessorEditor (TriPanAudioProcessor& p)
    : AudioProcessorEditor (&p), owner (p), field (p)
{
    addAndMakeVisible (field);

    showTriangulationButton.setToggleState (true, juce::dontSendNotification);
    showTriangulationButton.onClick = [this] { field.setShowTriangulation (showTriangulationButton.getToggleState()); };
    addAndMakeVisible (showTriangulationButton);

    resetButton.setTooltip ("Place the active sources and speakers on rings");
    resetButton.onClick = [this] { owner.resetLayoutToRings(); };
    addAndMakeVisible (resetButton);

    infoLabel.setJustificationType (juce::Justification::centredRight);
    infoLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (infoLabel);

    setResizable (true, true);
    setResizeLimits (420, 480, 1200, 1280);
    setSize (680, 740);

    updateInfoText();
    startTimerHz (4);
}

TriPanAudioProcessorEditor::~TriPanAudioProcessorEditor()
{
    stopTimer();
}

void TriPanAudioProcessorEditor::timerCallback()
{
    updateInfoText();
}

void TriPanAudioProcessorEditor::updateInfoText()
{
    const auto n = owner.getTotalNumInputChannels();
    const auto m = owner.getTotalNumOutputChannels();
    infoLabel.setText (juce::String (n) + (n == 1 ? " source" : " sources") + "  >  "
                           + juce::String (m) + (m == 1 ? " speaker" : " speakers"),
                       juce::dontSendNotification);
}

void TriPanAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff23272e));
}

void TriPanAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (40).reduced (10, 6);

    showTriangulationButton.setBounds (bar.removeFromLeft (170));
    resetButton.setBounds (bar.removeFromLeft (110));
    infoLabel.setBounds (bar);

    field.setBounds (area);
}
