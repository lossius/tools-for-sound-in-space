// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PositionTableWindow.cpp
    @brief Editable tables of source, speaker and virtual speaker positions, and the windows that hold them. */

#include "PositionTableWindow.h"
#include "Parameters.h"

#include <cmath>
#include <functional>

namespace
{
constexpr int kIndexColumnWidth = 44;   ///< Width of the row number column in pixels.
constexpr int kValueColumnWidth = 92;   ///< Width of the x, y and spread columns in pixels.
constexpr int kGap = 12;                ///< Space between two tables in pixels.
constexpr int kMaxRowsAtStart = 12;     ///< A newly opened window shows at most this many rows.

/** A number cell: a label that is edited with a single click and tells the table what was typed. */
class EditableCell final : public juce::Label
{
public:
    /** Makes the label editable by a single click. */
    EditableCell()
    {
        setEditable (false, true, false);
        setJustificationType (juce::Justification::centredRight);
        setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
        setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff10131a));
        setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
        setBorderSize ({ 0, 8, 0, 8 });
    }

    /** Shows `text`, unless the user is typing in the cell. */
    void showValue (const juce::String& text)
    {
        if (! isBeingEdited())
            setText (text, juce::dontSendNotification);
    }

    /** Called with the cell and what was typed, after the user pressed Return or left the cell. */
    std::function<void (EditableCell&)> onEdited;

    int row = 0;      ///< Zero-based row of the cell.
    int column = 0;   ///< Column ID of the cell.

private:
    /** Passes the typed text on. */
    void textWasEdited() override
    {
        if (onEdited)
            onEdited (*this);
    }
};

/** Sets a parameter to a real value inside a change gesture, so that hosts record it. */
void setParameter (TriPanAudioProcessor& processor, const juce::String& id, float value)
{
    if (auto* parameter = processor.getValueTreeState().getParameter (id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

/** Parses a plain decimal number (no letters other than an exponent, no empty text). */
bool parseNumber (const juce::String& text, double& out)
{
    const auto t = text.trim();
    if (t.isEmpty() || ! t.containsAnyOf ("0123456789") || ! t.containsOnly ("+-.0123456789eE"))
        return false;

    out = t.getDoubleValue();
    return std::isfinite (out);
}

/** Folds `value` into the running hash `seed`. */
void hashCombine (size_t& seed, double value)
{
    seed ^= std::hash<double> {} (value) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
}

//==============================================================================
PositionTable::PositionTable (TriPanAudioProcessor& p, Content c)
    : processor (p), content (c), table ("positions", nullptr)
{
    caption.setText (content == Content::Sources ? "Sources" : content == Content::Speakers ? "Speakers" : "Virtual speakers (manual)",
                     juce::dontSendNotification);
    caption.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    caption.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    addAndMakeVisible (caption);

    table.setModel (this);
    table.setRowHeight (kRowHeight);
    table.setHeaderHeight (kHeaderHeight);
    table.setMultipleSelectionEnabled (false);
    table.setClickingTogglesRowSelection (false);
    table.setOutlineThickness (1);
    table.getViewport()->setScrollBarThickness (kScrollBarWidth);
    table.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1b1e24));
    table.setColour (juce::ListBox::outlineColourId, juce::Colours::white.withAlpha (0.2f));
    table.getHeader().setColour (juce::TableHeaderComponent::backgroundColourId, juce::Colour (0xff343a46));
    table.getHeader().setColour (juce::TableHeaderComponent::textColourId, juce::Colours::white);
    table.getHeader().setColour (juce::TableHeaderComponent::outlineColourId, juce::Colours::white.withAlpha (0.2f));
    table.getHeader().setStretchToFitActive (false);

    // Fixed columns that can be neither resized, moved nor sorted.
    const auto flags = juce::TableHeaderComponent::visible;
    table.getHeader().addColumn ("#", indexColumn, kIndexColumnWidth, kIndexColumnWidth, kIndexColumnWidth, flags);
    table.getHeader().addColumn ("X (m)", xColumn, kValueColumnWidth, kValueColumnWidth, kValueColumnWidth, flags);
    table.getHeader().addColumn ("Y (m)", yColumn, kValueColumnWidth, kValueColumnWidth, kValueColumnWidth, flags);
    if (content == Content::Sources)
        table.getHeader().addColumn ("Spread (m)", spreadColumn, kValueColumnWidth, kValueColumnWidth, kValueColumnWidth, flags);

    addAndMakeVisible (table);

    lastSignature = signature();
}

PositionTable::~PositionTable()
{
    table.setModel (nullptr);
}

int PositionTable::getPreferredWidth() const noexcept
{
    const int valueColumns = content == Content::Sources ? 3 : 2;
    return kIndexColumnWidth + valueColumns * kValueColumnWidth + kScrollBarWidth + 2;
}

int PositionTable::getRowCount() const
{
    switch (content)
    {
        case Content::Sources:         return processor.getActiveSources();
        case Content::Speakers:        return processor.getActiveSpeakers();
        case Content::VirtualSpeakers: return (int) processor.getManualVirtualSpeakers().size();
    }

    return 0;
}

double PositionTable::getValue (int row, int column) const
{
    if (row < 0 || row >= getRowCount())
        return 0.0;

    const auto read = [this] (const juce::String& id) { return (double) processor.getValueTreeState().getRawParameterValue (id)->load(); };

    switch (content)
    {
        case Content::Sources:
            if (column == xColumn)      return read (tripan::params::sourceId (row, 'x'));
            if (column == yColumn)      return read (tripan::params::sourceId (row, 'y'));
            if (column == spreadColumn) return read (tripan::params::spreadId (row));
            break;

        case Content::Speakers:
            if (column == xColumn)      return read (tripan::params::speakerId (row, 'x'));
            if (column == yColumn)      return read (tripan::params::speakerId (row, 'y'));
            break;

        case Content::VirtualSpeakers:
        {
            const auto list = processor.getManualVirtualSpeakers();
            if (column == xColumn)      return list[(size_t) row].x;
            if (column == yColumn)      return list[(size_t) row].y;
            break;
        }
    }

    return 0.0;
}

juce::String PositionTable::format (double metres)
{
    if (std::abs (metres) < 0.0005)
        metres = 0.0; // no "-0.000"

    return juce::String (metres, 3);
}

juce::String PositionTable::setValueFromText (int row, int column, const juce::String& text)
{
    double typed = 0.0;
    if (row < 0 || row >= getRowCount() || ! parseNumber (text, typed))
        return format (getValue (row, column));

    const bool isSpread = content == Content::Sources && column == spreadColumn;
    const float limit = tripan::params::kCoordRange;
    const double value = isSpread ? juce::jlimit (0.0, (double) tripan::params::kMaxSpread, typed)
                                  : juce::jlimit (-(double) limit, (double) limit, typed);

    switch (content)
    {
        case Content::Sources:
            if (column == xColumn)      setParameter (processor, tripan::params::sourceId (row, 'x'), (float) value);
            if (column == yColumn)      setParameter (processor, tripan::params::sourceId (row, 'y'), (float) value);
            if (column == spreadColumn) setParameter (processor, tripan::params::spreadId (row), (float) value);
            break;

        case Content::Speakers:
            if (column == xColumn)      setParameter (processor, tripan::params::speakerId (row, 'x'), (float) value);
            if (column == yColumn)      setParameter (processor, tripan::params::speakerId (row, 'y'), (float) value);
            break;

        case Content::VirtualSpeakers:
        {
            auto list = processor.getManualVirtualSpeakers();
            if (column == xColumn)      list[(size_t) row].x = value;
            if (column == yColumn)      list[(size_t) row].y = value;
            processor.setManualVirtualSpeakers (list);
            break;
        }
    }

    return format (getValue (row, column));
}

size_t PositionTable::signature() const
{
    const int rows = getRowCount();
    size_t seed = (size_t) rows;

    for (int row = 0; row < rows; ++row)
        for (int column : { (int) xColumn, (int) yColumn, (int) spreadColumn })
            hashCombine (seed, getValue (row, column));

    return seed;
}

void PositionTable::refresh()
{
    const auto current = signature();
    if (current == lastSignature)
        return;

    lastSignature = current;
    table.updateContent();
    table.repaint();
}

void PositionTable::resized()
{
    auto area = getLocalBounds();
    caption.setBounds (area.removeFromTop (kCaptionHeight));
    table.setBounds (area);
}

void PositionTable::paintRowBackground (juce::Graphics& g, int row, int, int, bool)
{
    g.fillAll (row % 2 == 0 ? juce::Colour (0xff1b1e24) : juce::Colour (0xff22262e));
}

void PositionTable::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (column != indexColumn)
        return;

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String (row + 1), 0, 0, width - 8, height, juce::Justification::centredRight);
}

juce::Component* PositionTable::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
{
    if (column == indexColumn || row >= getRowCount())
    {
        delete existing;
        return nullptr;
    }

    auto* cell = dynamic_cast<EditableCell*> (existing);
    if (cell == nullptr)
    {
        delete existing;
        cell = new EditableCell();
        cell->onEdited = [this] (EditableCell& edited)
        {
            edited.setText (setValueFromText (edited.row, edited.column, edited.getText()), juce::dontSendNotification);
        };
    }

    cell->row = row;
    cell->column = column;
    cell->showValue (format (getValue (row, column)));
    return cell;
}

//==============================================================================
PositionTableContent::PositionTableContent (TriPanAudioProcessor& p, tripan::io::Kind k)
    : processor (p), kind (k)
{
    const bool sources = kind == tripan::io::Kind::Sources;

    countLabel.setText (sources ? "Number of sources" : "Number of speakers", juce::dontSendNotification);
    countLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    addAndMakeVisible (countLabel);

    // The same behaviour as the count sliders in the main window: +/- buttons, drag or type a value.
    countSlider.setSliderStyle (juce::Slider::IncDecButtons);
    countSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_AutoDirection);
    countSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 24);
    countSlider.setNumDecimalPlacesToDisplay (0);
    countSlider.setRange (1.0, 2.0, 1.0); // the real range is set by refresh() from the channel count

    auto* parameter = processor.getValueTreeState().getParameter (countParameterId());
    jassert (parameter != nullptr);
    countSlider.onDragStart = [this, parameter] { draggingCount = true; parameter->beginChangeGesture(); };
    countSlider.onDragEnd   = [this, parameter] { draggingCount = false; parameter->endChangeGesture(); };
    countSlider.onValueChange = [this, parameter]
    {
        if (syncing)
            return;

        const auto set = [&] { parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) countSlider.getValue())); };

        if (draggingCount)
        {
            set();
        }
        else
        {
            parameter->beginChangeGesture();
            set();
            parameter->endChangeGesture();
        }

        processor.refreshTriangulation();
        refresh();
    };
    addAndMakeVisible (countSlider);

    if (! sources)
    {
        virtualLabel.setJustificationType (juce::Justification::centredRight);
        virtualLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (virtualLabel);

        virtualModeBox.addItem ("Auto", (int) VirtualMode::Auto);
        virtualModeBox.addItem ("Manual", (int) VirtualMode::Manual);
        virtualModeBox.addItem ("Off", (int) VirtualMode::Off);
        virtualModeBox.setTooltip ("Auto: virtual speakers are added where the layout needs them. Manual: you place them; "
                                   "they get their own table here while the speakers are set to Edit. Off: real speakers only.");
        virtualModeBox.onChange = [this]
        {
            if (! syncing)
            {
                processor.setVirtualMode ((VirtualMode) virtualModeBox.getSelectedId());
                refresh();
            }
        };
        addAndMakeVisible (virtualModeBox);
    }

    mainTable = std::make_unique<PositionTable> (processor, sources ? PositionTable::Content::Sources : PositionTable::Content::Speakers);
    addAndMakeVisible (*mainTable);

    refresh();
    startTimerHz (10);
}

PositionTableContent::~PositionTableContent()
{
    stopTimer();
}

const juce::String& PositionTableContent::countParameterId() const
{
    return kind == tripan::io::Kind::Sources ? tripan::params::kNumSourcesId : tripan::params::kNumSpeakersId;
}

int PositionTableContent::getPreferredWidth() const
{
    const int topBar = kind == tripan::io::Kind::Sources ? 130 + 110
                                                         : 130 + 110 + 16 + 110 + 90;
    int tables = mainTable->getPreferredWidth();
    if (virtualTable != nullptr)
        tables += kGap + virtualTable->getPreferredWidth();

    return 2 * kMargin + std::max (topBar, tables);
}

int PositionTableContent::getHeightForRows (int rows) const
{
    return kMargin + kTopBarHeight + 8 + PositionTable::kCaptionHeight + PositionTable::kHeaderHeight
         + rows * PositionTable::kRowHeight + 2 + kMargin;
}

void PositionTableContent::refresh()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);
    bool layoutChanged = false;

    // The count: its range follows the channel count of the track.
    const bool sources = kind == tripan::io::Kind::Sources;
    const int channels = std::min (sources ? processor.getTotalNumInputChannels() : processor.getTotalNumOutputChannels(), tripan::kMaxChannels);
    const int maximum = std::max (channels, 1);
    const int inUse = sources ? processor.getActiveSources() : processor.getActiveSpeakers();
    countSlider.setEnabled (channels > 1);
    if (! juce::approximatelyEqual (countSlider.getMaximum(), (double) maximum))
        countSlider.setRange (1.0, (double) maximum, 1.0);
    if (! juce::approximatelyEqual (countSlider.getValue(), (double) inUse))
        countSlider.setValue ((double) inUse, juce::dontSendNotification);

    if (! sources && virtualModeBox.getSelectedId() != (int) processor.getVirtualMode())
        virtualModeBox.setSelectedId ((int) processor.getVirtualMode(), juce::dontSendNotification);

    // The virtual speaker table exists exactly while the virtual speakers can be edited by hand.
    const bool wantVirtualTable = ! sources && processor.canEditVirtualSpeakers();
    if (wantVirtualTable && virtualTable == nullptr)
    {
        virtualTable = std::make_unique<PositionTable> (processor, PositionTable::Content::VirtualSpeakers);
        addAndMakeVisible (*virtualTable);
        layoutChanged = true;
    }
    else if (! wantVirtualTable && virtualTable != nullptr)
    {
        virtualTable.reset();
        layoutChanged = true;
    }

    mainTable->refresh();
    if (virtualTable != nullptr)
        virtualTable->refresh();

    const int rows = std::max (mainTable->getRowCount(), virtualTable != nullptr ? virtualTable->getRowCount() : 0);
    if (rows != lastRows)
    {
        lastRows = rows;
        layoutChanged = true;
    }

    if (layoutChanged)
    {
        resized();
        if (onLayoutChanged)
            onLayoutChanged();
    }
}

void PositionTableContent::resized()
{
    auto area = getLocalBounds().reduced (kMargin);

    auto bar = area.removeFromTop (kTopBarHeight);
    countLabel.setBounds (bar.removeFromLeft (130));
    countSlider.setBounds (bar.removeFromLeft (110).reduced (0, 4));
    if (kind == tripan::io::Kind::Speakers)
    {
        bar.removeFromLeft (16);
        virtualLabel.setBounds (bar.removeFromLeft (110));
        virtualModeBox.setBounds (bar.removeFromLeft (90).reduced (0, 4));
    }

    area.removeFromTop (8);
    mainTable->setBounds (area.removeFromLeft (mainTable->getPreferredWidth()));
    if (virtualTable != nullptr)
    {
        area.removeFromLeft (kGap);
        virtualTable->setBounds (area.removeFromLeft (virtualTable->getPreferredWidth()));
    }
}

void PositionTableContent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff23272e));
}

//==============================================================================
PositionTableWindow::PositionTableWindow (TriPanAudioProcessor& processor, tripan::io::Kind kind)
    : juce::DocumentWindow (kind == tripan::io::Kind::Sources ? "TriPan - Sources" : "TriPan - Speakers",
                            juce::Colour (0xff23272e), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);

    // Plug-in windows are usually floating windows of the host; an ordinary window would open behind them.
    setAlwaysOnTop (true);

    content = new PositionTableContent (processor, kind);
    setContentOwned (content, false);
    setResizable (true, false);

    content->onLayoutChanged = [this] { applyLimits(); };
    applyLimits (true);
}

void PositionTableWindow::applyLimits (bool initial)
{
    // Never taller than the screen (the title bar is outside the content).
    int cap = 100000;
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        cap = std::max (200, juce::roundToInt (display->userBounds.getHeight()) - 80);

    const int width = content->getPreferredWidth();
    const int minHeight = std::min (content->getMinimumHeight(), cap);
    const int maxHeight = std::max (minHeight, std::min (content->getMaximumHeight(), cap));

    // The limits apply to the whole window, including a native title bar and frame.
    juce::BorderSize<int> frame;
    if (auto* peer = getPeer())
        frame = peer->getFrameSize();

    setResizeLimits (width + frame.getLeftAndRight(), minHeight + frame.getTopAndBottom(),
                     width + frame.getLeftAndRight(), maxHeight + frame.getTopAndBottom());

    const int wanted = initial ? content->getHeightForRows (std::min (content->getRowCount(), kMaxRowsAtStart)) : getHeight();
    setSize (width, juce::jlimit (minHeight, maxHeight, wanted));
}

void PositionTableWindow::show (juce::Component* around)
{
    if (! shownBefore && around != nullptr)
        centreAroundComponent (around, getWidth(), getHeight());

    shownBefore = true;
    setAlwaysOnTop (true);
    setVisible (true);
    applyLimits(); // the native frame size is known once the window is shown
    toFront (true);
}
