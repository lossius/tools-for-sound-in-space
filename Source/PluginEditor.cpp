#include "PluginEditor.h"
#include "Parameters.h"

TriPanAudioProcessorEditor::TriPanAudioProcessorEditor (TriPanAudioProcessor& p)
    : AudioProcessorEditor (&p), owner (p), field (p)
{
    addAndMakeVisible (field);

    for (auto* label : { &sourcesLabel, &speakersLabel })
    {
        label->setJustificationType (juce::Justification::centredRight);
        label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (label);
    }

    setUpCountSlider (sourceCount,  tripan::params::kNumSourcesId);
    setUpCountSlider (speakerCount, tripan::params::kNumSpeakersId);
    addAndMakeVisible (sourceCount);
    addAndMakeVisible (speakerCount);

    infoLabel.setJustificationType (juce::Justification::centredRight);
    infoLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    addAndMakeVisible (infoLabel);

    showTriangulationButton.setToggleState (true, juce::dontSendNotification);
    showTriangulationButton.onClick = [this] { field.setShowTriangulation (showTriangulationButton.getToggleState()); };
    addAndMakeVisible (showTriangulationButton);

    resetButton.setTooltip ("Place the sources and speakers in use on rings");
    resetButton.onClick = [this] { owner.resetLayoutToRings(); };
    addAndMakeVisible (resetButton);

    setResizable (true, true);
    setResizeLimits (440, 500, 1200, 1300);
    setSize (680, 780);

    syncControls();
    startTimerHz (10);
}

TriPanAudioProcessorEditor::~TriPanAudioProcessorEditor()
{
    stopTimer();
}

void TriPanAudioProcessorEditor::setUpCountSlider (juce::Slider& slider, const juce::String& parameterId)
{
    slider.setSliderStyle (juce::Slider::IncDecButtons);
    slider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_AutoDirection);
    slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 24);
    slider.setNumDecimalPlacesToDisplay (0);
    slider.setRange (1.0, 2.0, 1.0); // real range is set by syncControls() from the channel count

    auto* parameter = owner.getValueTreeState().getParameter (parameterId);
    jassert (parameter != nullptr);

    slider.onDragStart = [this, parameter] { dragging = true; parameter->beginChangeGesture(); };
    slider.onDragEnd   = [this, parameter] { dragging = false; parameter->endChangeGesture(); };

    slider.onValueChange = [this, &slider, parameter]
    {
        if (syncing)
            return;

        const auto set = [&] { parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) slider.getValue())); };

        if (dragging)
        {
            set();
        }
        else // typed value or a click on a +/- button
        {
            parameter->beginChangeGesture();
            set();
            parameter->endChangeGesture();
        }

        owner.refreshTriangulation();
    };
}

void TriPanAudioProcessorEditor::syncCountSlider (juce::Slider& slider, int effectiveValue, int channels)
{
    const int maximum = std::max (channels, 1);
    slider.setEnabled (channels > 1);

    if (! juce::approximatelyEqual (slider.getMaximum(), (double) maximum))
        slider.setRange (1.0, (double) maximum, 1.0);

    if (! juce::approximatelyEqual (slider.getValue(), (double) effectiveValue))
        slider.setValue ((double) effectiveValue, juce::dontSendNotification);
}

void TriPanAudioProcessorEditor::syncControls()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    const int channelsIn  = std::min (owner.getTotalNumInputChannels(),  tripan::kMaxChannels);
    const int channelsOut = std::min (owner.getTotalNumOutputChannels(), tripan::kMaxChannels);

    syncCountSlider (sourceCount,  owner.getActiveSources(),  channelsIn);
    syncCountSlider (speakerCount, owner.getActiveSpeakers(), channelsOut);

    infoLabel.setText ("track: " + juce::String (channelsIn) + " in, " + juce::String (channelsOut) + " out",
                       juce::dontSendNotification);
}

void TriPanAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff23272e));
}

void TriPanAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Top row: the number of sources and speakers in use.
    auto top = area.removeFromTop (40).reduced (10, 6);
    sourcesLabel.setBounds (top.removeFromLeft (64));
    sourceCount.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (14);
    speakersLabel.setBounds (top.removeFromLeft (70));
    speakerCount.setBounds (top.removeFromLeft (110));
    infoLabel.setBounds (top);

    // Second row: display options.
    auto bar = area.removeFromTop (34).reduced (10, 4);
    showTriangulationButton.setBounds (bar.removeFromLeft (170));
    resetButton.setBounds (bar.removeFromLeft (110));

    field.setBounds (area);
}
