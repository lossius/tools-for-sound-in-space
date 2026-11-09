// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginEditor.cpp
    @brief Layout, scaling, controls and file dialogs of the plugin window. */

#include "PluginEditor.h"
#include "Parameters.h"

TriPanAudioProcessorEditor::TriPanAudioProcessorEditor (TriPanAudioProcessor& p)
    : AudioProcessorEditor (&p), owner (p), field (p)
{
    addAndMakeVisible (content);
    content.addAndMakeVisible (field);

    for (auto* label : { &sourcesLabel, &speakersLabel, &virtualLabel })
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
    infoLabel.setFont (juce::FontOptions (12.0f));
    infoLabel.setMinimumHorizontalScale (0.6f); // long import/export messages are squeezed to fit
    content.addAndMakeVisible (infoLabel);

    // One menu per kind of position: "Edit" lets the user move them in this window, "View" locks them.
    const auto setUpModeBox = [this] (juce::ComboBox& box, const juce::String& what, bool editable, std::function<void (bool)> apply)
    {
        box.addItem ("Edit", 1);
        box.addItem ("View", 2);
        box.setSelectedId (editable ? 1 : 2, juce::dontSendNotification);
        box.setTooltip ("Edit: the " + what + " positions can be moved in this window. View: they are locked.");
        box.onChange = [this, &box, apply]
        {
            apply (box.getSelectedId() == 1);
            syncControls();
        };
        content.addAndMakeVisible (box);
    };
    setUpModeBox (sourcesModeBox,  "source",  owner.canEditSources(),  [this] (bool e) { owner.setSourcesEditable (e); });
    setUpModeBox (speakersModeBox, "speaker", owner.canEditSpeakers(), [this] (bool e) { owner.setSpeakersEditable (e); });

    // Virtual (imaginary) speakers: Auto finds them from the speaker layout, Manual lets the user place
    // them (double-click the pad to add or delete, drag to move; speakers must be set to Edit), Off uses
    // the real speakers only.
    virtualModeBox.addItem ("Auto", (int) VirtualMode::Auto);
    virtualModeBox.addItem ("Manual", (int) VirtualMode::Manual);
    virtualModeBox.addItem ("Off", (int) VirtualMode::Off);
    virtualModeBox.setSelectedId ((int) owner.getVirtualMode(), juce::dontSendNotification);
    virtualModeBox.setTooltip ("Auto: imaginary speakers are added where the layout needs them (open gaps, ring centre). "
                               "Manual: you place them (double-click the pad to add or delete, drag to move; speakers must be set to Edit). "
                               "Off: real speakers only.");
    virtualModeBox.onChange = [this]
    {
        owner.setVirtualMode ((VirtualMode) virtualModeBox.getSelectedId());
        syncControls();
    };
    content.addAndMakeVisible (virtualModeBox);

    showTriangulationButton.setToggleState (true, juce::dontSendNotification);
    showTriangulationButton.onClick = [this] { field.setShowTriangulation (showTriangulationButton.getToggleState()); };
    content.addAndMakeVisible (showTriangulationButton);

    resetButton.setTooltip ("Place the unlocked sources and speakers in use on rings");
    resetButton.onClick = [this]
    {
        owner.resetLayoutToRings (owner.canEditSources(), owner.canEditSpeakers());
    };
    content.addAndMakeVisible (resetButton);

    zoomInButton.setTooltip ("Zoom in on the area that contains the speakers");
    zoomInButton.onClick = [this] { field.zoomIn(); syncControls(); };
    content.addAndMakeVisible (zoomInButton);

    zoomOutButton.setTooltip ("Zoom out, back to the entire area");
    zoomOutButton.onClick = [this] { field.zoomOut(); syncControls(); };
    content.addAndMakeVisible (zoomOutButton);

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
    setUpFileButton (importSourcesButton,  Kind::Sources,  false, "Load source positions from an XML file");
    setUpFileButton (exportSpeakersButton, Kind::Speakers, true,  "Save the speaker positions in use to an XML file");
    setUpFileButton (importSpeakersButton, Kind::Speakers, false, "Load speaker positions from an XML file");

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

    const bool editSources = owner.canEditSources(), editSpeakers = owner.canEditSpeakers();
    if (sourcesModeBox.getSelectedId() != (editSources ? 1 : 2))
        sourcesModeBox.setSelectedId (editSources ? 1 : 2, juce::dontSendNotification);
    if (speakersModeBox.getSelectedId() != (editSpeakers ? 1 : 2))
        speakersModeBox.setSelectedId (editSpeakers ? 1 : 2, juce::dontSendNotification);
    resetButton.setEnabled (editSources || editSpeakers);
    if (virtualModeBox.getSelectedId() != (int) owner.getVirtualMode())
        virtualModeBox.setSelectedId ((int) owner.getVirtualMode(), juce::dontSendNotification);
    zoomInButton.setEnabled (field.canZoomIn());
    zoomOutButton.setEnabled (field.canZoomOut());

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
        if (report.speakersSetTo > 0)
            text << ", Number of Speakers set to " << report.speakersSetTo;
        else if (kind == tripan::io::Kind::Speakers)
            text << " (track has " << report.trackOutputs << (report.trackOutputs == 1 ? " output" : " outputs")
                 << "; count unchanged)";
        else if (report.inUse < report.applied)
            text << " (" << report.inUse << " in use)";
        if (report.spreadApplied > 0)
            text << ", spread for " << report.spreadApplied;
        if (report.virtualMode)
        {
            switch (*report.virtualMode)
            {
                case VirtualMode::Auto:   text << ", virtual speakers: auto"; break;
                case VirtualMode::Off:    text << ", virtual speakers: off"; break;
                case VirtualMode::Manual: text << ", " << report.virtualCount << " virtual " << (report.virtualCount == 1 ? "speaker" : "speakers"); break;
            }
        }
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
/** The folder last used in a file dialog (starts at the user's Documents folder), shared by all dialogs. */
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

    // One row per kind of position, with everything that belongs to it: how many are in use, whether
    // they can be edited, and export / import of their positions. Both rows share the same columns.
    const auto layoutRow = [&] (juce::Rectangle<int> row, juce::Label& label, juce::Slider& count, juce::ComboBox& menu,
                                juce::TextButton& exportButton, juce::TextButton& importButton)
    {
        row = row.reduced (10, 5);
        label.setBounds (row.removeFromLeft (70));
        count.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (8);
        menu.setBounds (row.removeFromLeft (84));
        row.removeFromLeft (16);
        const int buttonWidth = (row.getWidth() - 8) / 2;
        exportButton.setBounds (row.removeFromLeft (buttonWidth));
        row.removeFromLeft (8);
        importButton.setBounds (row);
    };

    layoutRow (area.removeFromTop (32), sourcesLabel, sourceCount, sourcesModeBox, exportSourcesButton, importSourcesButton);
    layoutRow (area.removeFromTop (32), speakersLabel, speakerCount, speakersModeBox, exportSpeakersButton, importSpeakersButton);

    // Third row: display options and zoom.
    auto bar = area.removeFromTop (32).reduced (10, 5);
    showTriangulationButton.setBounds (bar.removeFromLeft (150));
    resetButton.setBounds (bar.removeFromLeft (100));
    bar.removeFromLeft (8);
    zoomInButton.setBounds (bar.removeFromLeft (74));
    bar.removeFromLeft (4);
    zoomOutButton.setBounds (bar.removeFromLeft (74));

    // Fourth row: virtual speakers, and the line of text (track channels, import/export results).
    auto last = area.removeFromTop (32).reduced (10, 5);
    virtualLabel.setBounds (last.removeFromLeft (110));
    virtualModeBox.setBounds (last.removeFromLeft (90));
    last.removeFromLeft (12);
    infoLabel.setBounds (last);

    field.setBounds (area);
}
