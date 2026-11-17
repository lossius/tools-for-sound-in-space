// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file SpatialEditor.cpp
    @brief Layout, scaling, controls and file dialogs of the plugin window. */

#include "tlos/SpatialEditor.h"
#include "tlos/Parameters.h"

namespace tlos
{

SpatialEditor::SpatialEditor (SpatialProcessor& p, std::unique_ptr<FieldComponent> padComponent,
                              const juce::String& overlayToggleText, const juce::String& overlayToggleTip)
    : AudioProcessorEditor (&p), owner (p), field (std::move (padComponent)), settingsContent (p)
{
    addAndMakeVisible (content);
    content.addAndMakeVisible (*field);

    for (auto* label : { &sourcesLabel, &speakersLabel })
    {
        label->setJustificationType (juce::Justification::centredRight);
        label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        content.addAndMakeVisible (label);
    }

    setUpCountSlider (sourceCount,  params::kNumSourcesId);
    setUpCountSlider (speakerCount, params::kNumSpeakersId);
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

    settingsButton.setTooltip ("Open the settings: room size, level outside the speakers, virtual speakers, the plugin's own settings and OSC.");
    settingsButton.onClick = [this] { toggleSettingsWindow(); };
    content.addAndMakeVisible (settingsButton);

    overlayButton.setButtonText (overlayToggleText);
    overlayButton.setTooltip (overlayToggleTip);
    overlayButton.setToggleState (true, juce::dontSendNotification);
    overlayButton.onClick = [this] { field->setShowOverlay (overlayButton.getToggleState()); };
    content.addAndMakeVisible (overlayButton);
    overlayButton.setVisible (overlayToggleText.isNotEmpty());

    showLinksButton.setTooltip ("Draw the lines from each source to the speakers it feeds. Turn off for a less cluttered pad.");
    showLinksButton.setToggleState (true, juce::dontSendNotification);
    showLinksButton.onClick = [this] { field->setShowLinks (showLinksButton.getToggleState()); };
    content.addAndMakeVisible (showLinksButton);

    resetButton.setTooltip ("Place the unlocked sources and speakers in use on rings");
    resetButton.onClick = [this]
    {
        owner.resetLayoutToRings (owner.canEditSources(), owner.canEditSpeakers());
    };
    content.addAndMakeVisible (resetButton);

    zoomInButton.setTooltip ("Zoom in on the area that contains the speakers");
    zoomInButton.onClick = [this] { field->zoomIn(); syncControls(); };
    content.addAndMakeVisible (zoomInButton);

    zoomOutButton.setTooltip ("Zoom out, back to the entire area");
    zoomOutButton.onClick = [this] { field->zoomOut(); syncControls(); };
    content.addAndMakeVisible (zoomOutButton);

    groupButton.setTooltip ("Group the selected sources, so that they move together (Cmd+G). Select with Shift-click or by dragging a rectangle on the pad.");
    groupButton.onClick = [this] { field->groupSelection(); syncControls(); };
    content.addAndMakeVisible (groupButton);

    ungroupButton.setTooltip ("Dissolve the groups of the selected sources (Cmd+Shift+G)");
    ungroupButton.onClick = [this] { field->ungroupSelection(); syncControls(); };
    content.addAndMakeVisible (ungroupButton);

    field->onSelectionChanged = [this] { syncControls(); };

    const auto setUpFileButton = [this] (juce::TextButton& b, io::Kind kind, bool isExport, const juce::String& tip)
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
    using io::Kind;
    setUpFileButton (exportSourcesButton,  Kind::Sources,  true,  "Save the source positions in use to an XML file");
    setUpFileButton (importSourcesButton,  Kind::Sources,  false, "Load source positions from an XML file");
    setUpFileButton (exportSpeakersButton, Kind::Speakers, true,  "Save the speaker positions in use to an XML file");
    setUpFileButton (importSpeakersButton, Kind::Speakers, false, "Load speaker positions from an XML file");

    sourcesTableButton.setTooltip ("Open or close a table with the x, y and spread of every source, where the values can be typed in");
    sourcesTableButton.onClick = [this] { toggleTableWindow (Kind::Sources); };
    content.addAndMakeVisible (sourcesTableButton);

    speakersTableButton.setTooltip ("Open or close a table with the x and y of every speaker (and of the manual virtual speakers), where the values can be typed in");
    speakersTableButton.onClick = [this] { toggleTableWindow (Kind::Speakers); };
    content.addAndMakeVisible (speakersTableButton);

    // Footer with links; small and quiet, as it is secondary to the plugin itself.
    for (auto* link : { &authorLink, &helpLink, &institutionLink })
    {
        link->setFont (juce::FontOptions (11.0f), false, juce::Justification::centred);
        link->setColour (juce::HyperlinkButton::textColourId, juce::Colours::white.withAlpha (0.55f));
        content.addAndMakeVisible (*link);
    }

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

SpatialEditor::~SpatialEditor()
{
    stopTimer();
    sourcesTableWindow.reset();
    speakersTableWindow.reset();
    settingsWindow.reset();
}

PositionTableWindow* SpatialEditor::getTableWindow (io::Kind kind) noexcept
{
    return (kind == io::Kind::Sources ? sourcesTableWindow : speakersTableWindow).get();
}

void SpatialEditor::toggleTableWindow (io::Kind kind)
{
    if (auto* window = getTableWindow (kind); window != nullptr && window->isVisible())
        window->setVisible (false);
    else
        showTableWindow (kind);
}

PositionTableWindow& SpatialEditor::showTableWindow (io::Kind kind)
{
    auto& window = kind == io::Kind::Sources ? sourcesTableWindow : speakersTableWindow;
    if (window == nullptr)
        window = std::make_unique<PositionTableWindow> (owner, kind);

    window->show (this);
    return *window;
}

void SpatialEditor::setUpCountSlider (juce::Slider& slider, const juce::String& parameterId)
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

        owner.refreshLayout();
    };
}

void SpatialEditor::syncCountSlider (juce::Slider& slider, int effectiveValue, int channels)
{
    const int maximum = std::max (channels, 1);
    slider.setEnabled (channels > 1);

    if (! juce::approximatelyEqual (slider.getMaximum(), (double) maximum))
        slider.setRange (1.0, (double) maximum, 1.0);

    if (! juce::approximatelyEqual (slider.getValue(), (double) effectiveValue))
        slider.setValue ((double) effectiveValue, juce::dontSendNotification);
}

void SpatialEditor::syncControls()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    const int channelsIn  = std::min (owner.getTotalNumInputChannels(),  tlos::kMaxChannels);
    const int channelsOut = std::min (owner.getTotalNumOutputChannels(), tlos::kMaxChannels);

    syncCountSlider (sourceCount,  owner.getActiveSources(),  channelsIn);
    syncCountSlider (speakerCount, owner.getActiveSpeakers(), channelsOut);

    const bool editSources = owner.canEditSources(), editSpeakers = owner.canEditSpeakers();
    if (sourcesModeBox.getSelectedId() != (editSources ? 1 : 2))
        sourcesModeBox.setSelectedId (editSources ? 1 : 2, juce::dontSendNotification);
    if (speakersModeBox.getSelectedId() != (editSpeakers ? 1 : 2))
        speakersModeBox.setSelectedId (editSpeakers ? 1 : 2, juce::dontSendNotification);
    resetButton.setEnabled (editSources || editSpeakers);
    settingsContent.refresh();
    zoomInButton.setEnabled (field->canZoomIn());
    zoomOutButton.setEnabled (field->canZoomOut());
    groupButton.setEnabled (field->canGroupSelection());
    ungroupButton.setEnabled (field->canUngroupSelection());

    const bool showingStatus = statusText.isNotEmpty() && juce::Time::getMillisecondCounter() < statusUntil;
    infoLabel.setText (showingStatus ? statusText
                                     : "track: " + juce::String (channelsIn) + " in, " + juce::String (channelsOut) + " out",
                       juce::dontSendNotification);
}

SettingsWindow& SpatialEditor::showSettingsWindow()
{
    if (settingsWindow == nullptr)
        settingsWindow = std::make_unique<SettingsWindow> (owner.getName() + " - Settings", settingsContent);

    settingsWindow->show (this);
    return *settingsWindow;
}

void SpatialEditor::toggleSettingsWindow()
{
    if (settingsWindow != nullptr && settingsWindow->isVisible())
        settingsWindow->setVisible (false);
    else
        showSettingsWindow();
}

void SpatialEditor::showStatus (const juce::String& text)
{
    statusText = text;
    statusUntil = juce::Time::getMillisecondCounter() + 5000;
    syncControls();
}

juce::Result SpatialEditor::exportPositionsTo (io::Kind kind, const juce::File& file)
{
    const auto result = io::save (owner, kind, file);
    if (result.wasOk())
    {
        const int n = kind == io::Kind::Sources ? owner.getActiveSources() : owner.getActiveSpeakers();
        showStatus ("Exported " + juce::String (n) + " " + io::describe (kind) + (n == 1 ? " position" : " positions"));
    }
    return result;
}

juce::Result SpatialEditor::importPositionsFrom (io::Kind kind, const juce::File& file)
{
    io::ImportReport report;
    const auto result = io::load (owner, kind, file, report);
    if (result.wasOk())
    {
        auto text = "Imported " + juce::String (report.applied) + " " + io::describe (kind)
                  + (report.applied == 1 ? " position" : " positions");
        if (report.speakersSetTo > 0)
            text << ", Number of Speakers set to " << report.speakersSetTo;
        else if (kind == io::Kind::Speakers)
            text << " (track has " << report.trackOutputs << (report.trackOutputs == 1 ? " output" : " outputs")
                 << "; count unchanged)";
        else if (report.inUse < report.applied)
            text << " (" << report.inUse << " in use)";
        if (report.spreadApplied > 0)
            text << ", spread for " << report.spreadApplied;
        if (report.calibrationApplied > 0)
            text << ", trim/mute/solo/exclude for " << report.calibrationApplied;
        if (report.groupCount > 0)
            text << ", " << report.groupCount << (report.groupCount == 1 ? " group" : " groups");
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

void SpatialEditor::chooseFileAndExport (io::Kind kind)
{
    const auto name = io::describe (kind);
    fileChooser = std::make_unique<juce::FileChooser> ("Export " + name + " positions",
                                                       lastDirectory().getChildFile (owner.getName() + "-" + name + "s.xml"),
                                                       "*.xml", true, false, this);

    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync (flags, [safe = juce::Component::SafePointer<SpatialEditor> (this), kind] (const juce::FileChooser& chooser)
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

void SpatialEditor::chooseFileAndImport (io::Kind kind)
{
    const auto name = io::describe (kind);
    fileChooser = std::make_unique<juce::FileChooser> ("Import " + name + " positions", lastDirectory(), "*.xml", true, false, this);

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (flags, [safe = juce::Component::SafePointer<SpatialEditor> (this), kind] (const juce::FileChooser& chooser)
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

void SpatialEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff23272e));
}

void SpatialEditor::resized()
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

void SpatialEditor::layoutContent()
{
    auto area = content.getLocalBounds();

    // The pad goes first, because the rows and the footer line up with its borders: nothing in the top rows sticks
    // out beyond the pad on the left or on the right.
    constexpr int rowHeight = 32, footerHeight = 28;
    constexpr int rowCount = 4;
    field->setBounds (area.withTrimmedTop (rowCount * rowHeight).withTrimmedBottom (footerHeight));
    const auto plane = field->planeBounds().translated ((float) field->getX(), (float) field->getY()).toNearestInt();
    const auto nextRow = [&] { return area.removeFromTop (rowHeight).withLeft (plane.getX()).withRight (plane.getRight()).reduced (0, 5); };

    // One row per kind of position, with everything that belongs to it: how many are in use, whether
    // they can be edited, and export / import of their positions. Both rows share the same columns.
    const auto layoutRow = [&] (juce::Rectangle<int> row, juce::Label& label, juce::Slider& count, juce::ComboBox& menu,
                                juce::TextButton& exportButton, juce::TextButton& importButton, juce::TextButton& tableButton)
    {
        label.setBounds (row.removeFromLeft (70));
        count.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (8);
        menu.setBounds (row.removeFromLeft (84));
        row.removeFromLeft (16);
        tableButton.setBounds (row.removeFromRight (80)); // at the right end of the row
        row.removeFromRight (16);
        const int buttonWidth = (row.getWidth() - 8) / 2;
        exportButton.setBounds (row.removeFromLeft (buttonWidth));
        row.removeFromLeft (8);
        importButton.setBounds (row);
    };

    layoutRow (nextRow(), sourcesLabel, sourceCount, sourcesModeBox, exportSourcesButton, importSourcesButton, sourcesTableButton);
    layoutRow (nextRow(), speakersLabel, speakerCount, speakersModeBox, exportSpeakersButton, importSpeakersButton, speakersTableButton);

    // Third row: display options and zoom.
    auto bar = nextRow();
    if (overlayButton.isVisible())
        overlayButton.setBounds (bar.removeFromLeft (140));
    showLinksButton.setBounds (bar.removeFromLeft (105));
    resetButton.setBounds (bar.removeFromLeft (90));
    bar.removeFromLeft (8);
    zoomInButton.setBounds (bar.removeFromLeft (34));
    bar.removeFromLeft (4);
    zoomOutButton.setBounds (bar.removeFromLeft (34));
    bar.removeFromLeft (16);
    groupButton.setBounds (bar.removeFromLeft (62));
    bar.removeFromLeft (4);
    ungroupButton.setBounds (bar.removeFromLeft (74));

    // The buttons that open windows (Table..., Table..., Settings...) are in one column at the right end of the rows.
    settingsButton.setBounds (bar.removeFromRight (80));

    // Fourth row: the line of text (track channels, import/export results).
    infoLabel.setBounds (nextRow());

    // Footer: links at the left, in the centre and at the right, each only as wide as its text so that
    // clicking the empty space between them does nothing.
    constexpr int inset = 4; // see below
    auto footer = area.removeFromBottom (28).withTrimmedBottom (8).withTrimmedTop (2);
    // The text sits centred in its button, which is `inset` px wider than the text on each side. The outer links start
    // `inset` px outside the pad's borders, so that their text lines up with them.
    footer = footer.withLeft (plane.getX() - inset).withRight (plane.getRight() + inset);
    const auto fitted = [] (const juce::HyperlinkButton& link)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (juce::FontOptions (11.0f), link.getButtonText(), 0.0f, 0.0f);
        return juce::roundToInt (std::ceil (glyphs.getBoundingBox (0, -1, true).getWidth())) + 2 * inset;
    };
    authorLink.setBounds (footer.removeFromLeft (fitted (authorLink)));
    institutionLink.setBounds (footer.removeFromRight (fitted (institutionLink)));
    // Centred on the window, not on the space left between the other two links (which differ in width),
    // so that it lines up with "Back" above the pad.
    helpLink.setBounds (juce::Rectangle<int> (fitted (helpLink), footer.getHeight()).withCentre ({ content.getWidth() / 2, footer.getCentreY() }));

}
}
