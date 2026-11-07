#include "PluginEditor.h"
#include "Parameters.h"

TriPanAudioProcessorEditor::TriPanAudioProcessorEditor (TriPanAudioProcessor& p)
    : AudioProcessorEditor (&p), owner (p), field (p)
{
    addAndMakeVisible (content);
    content.addAndMakeVisible (field);

    for (auto* label : { &sourcesLabel, &speakersLabel, &modeLabel })
    {
        label->setJustificationType (juce::Justification::centredRight);
        label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        content.addAndMakeVisible (label);
    }

    setUpCountSlider (sourceCount,  tripan::params::kNumSourcesId);
    setUpCountSlider (speakerCount, tripan::params::kNumSpeakersId);
    content.addAndMakeVisible (sourceCount);
    content.addAndMakeVisible (speakerCount);

    infoLabel.setJustificationType (juce::Justification::centredRight);
    infoLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    content.addAndMakeVisible (infoLabel);

    modeBox.addItem ("1: Move sources", (int) EditMode::Sources);
    modeBox.addItem ("2: Move speakers", (int) EditMode::Speakers);
    modeBox.addItem ("3: Move sources and speakers", (int) EditMode::Both);
    modeBox.addItem ("4: View only (positions locked)", (int) EditMode::View);
    modeBox.setSelectedId ((int) owner.getEditMode(), juce::dontSendNotification);
    modeBox.setTooltip ("Which positions can be changed in this window");
    modeBox.onChange = [this]
    {
        owner.setEditMode ((EditMode) modeBox.getSelectedId());
        syncControls();
    };
    content.addAndMakeVisible (modeBox);

    showTriangulationButton.setToggleState (true, juce::dontSendNotification);
    showTriangulationButton.onClick = [this] { field.setShowTriangulation (showTriangulationButton.getToggleState()); };
    content.addAndMakeVisible (showTriangulationButton);

    resetButton.setTooltip ("Place the unlocked sources and speakers in use on rings");
    resetButton.onClick = [this]
    {
        const auto mode = owner.getEditMode();
        owner.resetLayoutToRings (canEditSources (mode), canEditSpeakers (mode));
    };
    content.addAndMakeVisible (resetButton);

    const auto setUpFileButton = [this] (juce::TextButton& b, tripan::io::Kind kind, bool isExport, const juce::String& tip)
    {
        b.setTooltip (tip);
        b.onClick = [this, kind, isExport]
        {
            if (isExport)
                chooseFileAndExport (kind);
            else
                chooseFileAndImport (kind);
        };
        content.addAndMakeVisible (b);
    };
    using tripan::io::Kind;
    setUpFileButton (exportSourcesButton,  Kind::Sources,  true,  "Save the source positions in use to an XML file");
    setUpFileButton (importSourcesButton,  Kind::Sources,  false, "Load source positions from an XML file (needs a mode that unlocks sources)");
    setUpFileButton (exportSpeakersButton, Kind::Speakers, true,  "Save the speaker positions in use to an XML file");
    setUpFileButton (importSpeakersButton, Kind::Speakers, false, "Load speaker positions from an XML file (needs a mode that unlocks speakers)");

    // Scalable GUI: the base size is the minimum, the aspect ratio is fixed.
    setResizable (true, true);
    setResizeLimits (kBaseWidth, kBaseHeight, kBaseWidth * kMaxScale, kBaseHeight * kMaxScale);
    getConstrainer()->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);

    const int savedWidth = juce::jlimit (kBaseWidth, kBaseWidth * kMaxScale, owner.getEditorWidth() > 0 ? owner.getEditorWidth() : kBaseWidth);
    setSize (savedWidth, juce::roundToInt ((double) savedWidth * kBaseHeight / kBaseWidth));
    sizeIsRestored = true;

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

    const auto mode = owner.getEditMode();
    if (modeBox.getSelectedId() != (int) mode)
        modeBox.setSelectedId ((int) mode, juce::dontSendNotification);
    resetButton.setEnabled (mode != EditMode::View);
    // Importing changes positions, so it follows the same locks as dragging.
    importSourcesButton.setEnabled (canEditSources (mode));
    importSpeakersButton.setEnabled (canEditSpeakers (mode));

    const bool showingStatus = statusText.isNotEmpty() && juce::Time::getMillisecondCounter() < statusUntil;
    infoLabel.setText (showingStatus ? statusText
                                     : "track: " + juce::String (channelsIn) + " in, " + juce::String (channelsOut) + " out",
                       juce::dontSendNotification);
}

void TriPanAudioProcessorEditor::showStatus (const juce::String& text)
{
    statusText = text;
    statusUntil = juce::Time::getMillisecondCounter() + 5000;
    syncControls();
}

juce::Result TriPanAudioProcessorEditor::exportPositionsTo (tripan::io::Kind kind, const juce::File& file)
{
    const auto result = tripan::io::save (owner, kind, file);
    if (result.wasOk())
    {
        const int n = kind == tripan::io::Kind::Sources ? owner.getActiveSources() : owner.getActiveSpeakers();
        showStatus ("Exported " + juce::String (n) + " " + tripan::io::describe (kind) + (n == 1 ? " position" : " positions"));
    }
    return result;
}

juce::Result TriPanAudioProcessorEditor::importPositionsFrom (tripan::io::Kind kind, const juce::File& file)
{
    tripan::io::ImportReport report;
    const auto result = tripan::io::load (owner, kind, file, report);
    if (result.wasOk())
    {
        auto text = "Imported " + juce::String (report.applied) + " " + tripan::io::describe (kind)
                  + (report.applied == 1 ? " position" : " positions");
        if (report.inUse < report.applied)
            text << " (" << report.inUse << " in use)";
        if (report.clamped > 0)
            text << ", " << report.clamped << " clamped";
        if (report.skipped > 0)
            text << ", " << report.skipped << " skipped";
        showStatus (text);
    }
    return result;
}

namespace
{
juce::File& lastDirectory()
{
    static juce::File dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    return dir;
}
}

void TriPanAudioProcessorEditor::chooseFileAndExport (tripan::io::Kind kind)
{
    const auto name = tripan::io::describe (kind);
    fileChooser = std::make_unique<juce::FileChooser> ("Export " + name + " positions",
                                                       lastDirectory().getChildFile ("TriPan-" + name + "s.xml"),
                                                       "*.xml", true, false, this);

    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync (flags, [safe = juce::Component::SafePointer<TriPanAudioProcessorEditor> (this), kind] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (safe == nullptr || file == juce::File())
            return;

        if (! file.hasFileExtension ("xml"))
            file = file.withFileExtension ("xml");

        lastDirectory() = file.getParentDirectory();
        const auto result = safe->exportPositionsTo (kind, file);
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed", result.getErrorMessage());
    });
}

void TriPanAudioProcessorEditor::chooseFileAndImport (tripan::io::Kind kind)
{
    const auto name = tripan::io::describe (kind);
    fileChooser = std::make_unique<juce::FileChooser> ("Import " + name + " positions", lastDirectory(), "*.xml", true, false, this);

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (flags, [safe = juce::Component::SafePointer<TriPanAudioProcessorEditor> (this), kind] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (safe == nullptr || file == juce::File())
            return;

        lastDirectory() = file.getParentDirectory();
        const auto result = safe->importPositionsFrom (kind, file);
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import failed", result.getErrorMessage());
    });
}

void TriPanAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff23272e));
}

void TriPanAudioProcessorEditor::resized()
{
    // Uniform scale; if the host ignores the aspect ratio the content is centred.
    scale = std::min ((float) getWidth() / (float) kBaseWidth, (float) getHeight() / (float) kBaseHeight);
    const float dx = ((float) getWidth()  - (float) kBaseWidth  * scale) * 0.5f;
    const float dy = ((float) getHeight() - (float) kBaseHeight * scale) * 0.5f;

    content.setBounds (0, 0, kBaseWidth, kBaseHeight);
    content.setTransform (juce::AffineTransform::scale (scale).translated (dx, dy));
    layoutContent();

    if (sizeIsRestored) // not while the constructor sets the initial size
        owner.setEditorWidth (getWidth());
}

void TriPanAudioProcessorEditor::layoutContent()
{
    auto area = content.getLocalBounds();

    // Top row: the number of sources and speakers in use.
    auto top = area.removeFromTop (40).reduced (10, 6);
    sourcesLabel.setBounds (top.removeFromLeft (64));
    sourceCount.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (14);
    speakersLabel.setBounds (top.removeFromLeft (70));
    speakerCount.setBounds (top.removeFromLeft (110));
    infoLabel.setBounds (top);

    // Second row: edit mode and display options.
    auto bar = area.removeFromTop (34).reduced (10, 4);
    modeLabel.setBounds (bar.removeFromLeft (40));
    modeBox.setBounds (bar.removeFromLeft (230));
    bar.removeFromLeft (12);
    showTriangulationButton.setBounds (bar.removeFromLeft (150));
    resetButton.setBounds (bar.removeFromLeft (100));

    // Third row: export / import of positions.
    auto files = area.removeFromTop (34).reduced (10, 4);
    const int w = (files.getWidth() - 3 * 8) / 4;
    exportSourcesButton.setBounds (files.removeFromLeft (w));
    files.removeFromLeft (8);
    importSourcesButton.setBounds (files.removeFromLeft (w));
    files.removeFromLeft (8);
    exportSpeakersButton.setBounds (files.removeFromLeft (w));
    files.removeFromLeft (8);
    importSpeakersButton.setBounds (files);

    field.setBounds (area);
}
