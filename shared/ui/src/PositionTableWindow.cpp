// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PositionTableWindow.cpp
    @brief Editable tables of source, speaker and virtual speaker positions, and the windows that hold them. */

#include "tlos/PositionTableWindow.h"
#include "tlos/FieldComponent.h"
#include "tlos/Parameters.h"
#include "tlos/Polar.h"

#include <cmath>
#include <functional>

namespace tlos
{

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

/** A check box in a table cell (mute, solo, exclude). */
class SwitchCell final : public juce::ToggleButton
{
public:
    /** Makes a check box without text. */
    SwitchCell() { setClickingTogglesState (true); }

    int row = 0;      ///< Zero-based row of the cell.
    int column = 0;   ///< Column ID of the cell.
};

/** Sets a parameter to a real value inside a change gesture, so that hosts record it. */
void setParameter (SpatialProcessor& processor, const juce::String& id, float value)
{
    if (auto* parameter = processor.getValueTreeState().getParameter (id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

/** Sets several parameters to real values, all inside change gestures begun together and ended together. */
void setParameters (SpatialProcessor& processor, std::initializer_list<std::pair<juce::String, float>> changes)
{
    std::vector<juce::RangedAudioParameter*> touched;
    for (const auto& change : changes)
        if (auto* parameter = processor.getValueTreeState().getParameter (change.first))
            touched.push_back (parameter);

    for (auto* parameter : touched)
        parameter->beginChangeGesture();

    for (const auto& change : changes)
        if (auto* parameter = processor.getValueTreeState().getParameter (change.first))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (change.second));

    for (auto* parameter : touched)
        parameter->endChangeGesture();
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
PositionTable::PositionTable (SpatialProcessor& p, Content c)
    : processor (p), content (c), table ("positions", nullptr)
{
    caption.setText (content == Content::Sources ? "Sources" : content == Content::Speakers ? processor.getOutputNoun() + "s" : "Virtual speakers (manual)",
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
    if (content == Content::Sources && processor.usesSpread())
        table.getHeader().addColumn ("Spread (m)", spreadColumn, kValueColumnWidth, kValueColumnWidth, kValueColumnWidth, flags);
    if (content == Content::Sources)
        table.getHeader().addColumn ("Group", groupColumn, kGroupColumnWidth, kGroupColumnWidth, kGroupColumnWidth, flags);
    if (content != Content::VirtualSpeakers)
    {
        table.getHeader().addColumn ("Trim (dB)", trimColumn, kValueColumnWidth, kValueColumnWidth, kValueColumnWidth, flags);
        table.getHeader().addColumn ("Mute", muteColumn, kSwitchColumnWidth, kSwitchColumnWidth, kSwitchColumnWidth, flags);
        table.getHeader().addColumn ("Solo", soloColumn, kSwitchColumnWidth, kSwitchColumnWidth, kSwitchColumnWidth, flags);
    }
    if (content == Content::Speakers)
        table.getHeader().addColumn ("Excl.", excludeColumn, kSwitchColumnWidth, kSwitchColumnWidth, kSwitchColumnWidth, flags);
    for (int k = 0; k < (int) extraColumns().size(); ++k)
        table.getHeader().addColumn (extraColumns()[(size_t) k].title, extraColumn + k, kValueColumnWidth, kValueColumnWidth, kValueColumnWidth, flags);

    addAndMakeVisible (table);

    updateCoordinateHeaders();
    lastSignature = signature();
}

PositionTable::~PositionTable()
{
    table.setModel (nullptr);
}

int PositionTable::getPreferredWidth() const noexcept
{
    const int valueColumns = content == Content::Sources ? (processor.usesSpread() ? 4 : 3) : content == Content::Speakers ? 3 : 2; // x, y, (spread), (trim)
    const int groupWidth = content == Content::Sources ? kGroupColumnWidth : 0;
    const int switches = content == Content::Sources ? 2 : content == Content::Speakers ? 3 : 0;    // mute, solo, (exclude)
    const int extra = (int) extraColumns().size() * kValueColumnWidth;
    return kIndexColumnWidth + valueColumns * kValueColumnWidth + extra + groupWidth + switches * kSwitchColumnWidth + kScrollBarWidth + 2;
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

std::vector<SpeakerColumn> PositionTable::extraColumns() const
{
    switch (content)
    {
        case Content::Sources:         return processor.getSourceColumns();
        case Content::Speakers:        return processor.getSpeakerColumns();
        case Content::VirtualSpeakers: break;
    }

    return {};
}

void PositionTable::updateCoordinateHeaders()
{
    shownMode = processor.getCoordinateMode();
    const bool polar = shownMode == SpatialProcessor::CoordinateMode::Polar;
    table.getHeader().setColumnName (xColumn, polar ? juce::String ("Azimuth (") + juce::String::charToString (0xb0) + ")" : juce::String ("X (m)"));
    table.getHeader().setColumnName (yColumn, polar ? juce::String ("Distance (m)") : juce::String ("Y (m)"));
}

Vec2 PositionTable::positionOf (int row) const
{
    if (content == Content::VirtualSpeakers)
    {
        const auto list = processor.getManualVirtualSpeakers();
        return row >= 0 && row < (int) list.size() ? Vec2 { list[(size_t) row].x, list[(size_t) row].y } : Vec2 {};
    }

    const auto read = [this] (const juce::String& id) { return (double) processor.getValueTreeState().getRawParameterValue (id)->load(); };
    const bool sources = content == Content::Sources;
    return { read (sources ? params::sourceId (row, 'x') : params::speakerId (row, 'x')),
             read (sources ? params::sourceId (row, 'y') : params::speakerId (row, 'y')) };
}

double PositionTable::getValue (int row, int column) const
{
    if (row < 0 || row >= getRowCount())
        return 0.0;

    if ((column == xColumn || column == yColumn) && processor.getCoordinateMode() == SpatialProcessor::CoordinateMode::Polar)
    {
        const auto polar = toPolar (positionOf (row), lastAzimuth[(size_t) row]);
        if (polar.distance > 1e-9)
            lastAzimuth[(size_t) row] = polar.azimuthDegrees;
        return column == xColumn ? polar.azimuthDegrees : polar.distance;
    }

    const auto read = [this] (const juce::String& id) { return (double) processor.getValueTreeState().getRawParameterValue (id)->load(); };

    switch (content)
    {
        case Content::Sources:
            if (column == xColumn)      return read (params::sourceId (row, 'x'));
            if (column == yColumn)      return read (params::sourceId (row, 'y'));
            if (column == spreadColumn) return read (params::spreadId (row));
            if (column == trimColumn)   return read (params::sourceTrimId (row));
            if (column == muteColumn)   return read (params::sourceMuteId (row)) >= 0.5 ? 1.0 : 0.0;
            if (column == soloColumn)   return read (params::sourceSoloId (row)) >= 0.5 ? 1.0 : 0.0;
            if (column >= extraColumn)
                if (const auto columns = extraColumns(); column - extraColumn < (int) columns.size())
                    return read (columns[(size_t) (column - extraColumn)].idFor (row));
            break;

        case Content::Speakers:
            if (column == xColumn)      return read (params::speakerId (row, 'x'));
            if (column == yColumn)      return read (params::speakerId (row, 'y'));
            if (column == trimColumn)   return read (params::speakerTrimId (row));
            if (column == muteColumn)   return read (params::speakerMuteId (row)) >= 0.5 ? 1.0 : 0.0;
            if (column == soloColumn)   return read (params::speakerSoloId (row)) >= 0.5 ? 1.0 : 0.0;
            if (column == excludeColumn) return processor.isSpeakerExcluded (row) ? 1.0 : 0.0;
            if (column >= extraColumn)
                if (const auto columns = extraColumns(); column - extraColumn < (int) columns.size())
                    return read (columns[(size_t) (column - extraColumn)].idFor (row));
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

juce::String PositionTable::formatCell (int column, double value) const
{
    if (column >= extraColumn)
    {
        const auto columns = extraColumns();
        return juce::String (value, column - extraColumn < (int) columns.size() ? columns[(size_t) (column - extraColumn)].decimals : 2);
    }

    if (column == xColumn && shownMode == SpatialProcessor::CoordinateMode::Polar)
        return juce::String (std::abs (value) < 0.05 ? 0.0 : value, 1);   // degrees: one decimal, no "-0.0"

    if (column != trimColumn)
        return format (value);

    return juce::String (std::abs (value) < 0.05 ? 0.0 : value, 1); // no "-0.0"
}

void PositionTable::setSwitch (int row, int column, bool on)
{
    if (row < 0 || row >= getRowCount())
        return;

    const bool sources = content == Content::Sources;

    if (column == muteColumn) setParameter (processor, sources ? params::sourceMuteId (row) : params::speakerMuteId (row), on ? 1.0f : 0.0f);
    if (column == soloColumn) setParameter (processor, sources ? params::sourceSoloId (row) : params::speakerSoloId (row), on ? 1.0f : 0.0f);
    if (column == excludeColumn && content == Content::Speakers) processor.setSpeakerExcluded (row, on);
}

juce::String PositionTable::setValueFromText (int row, int column, const juce::String& text)
{
    double typed = 0.0;
    if (row < 0 || row >= getRowCount() || ! parseNumber (text, typed))
        return formatCell (column, getValue (row, column));

    // Polar mode: the typed azimuth or distance replaces that part of the current position, which is then
    // written as x and y (one undo step).
    if ((column == xColumn || column == yColumn) && processor.getCoordinateMode() == SpatialProcessor::CoordinateMode::Polar)
    {
        const auto current = toPolar (positionOf (row), lastAzimuth[(size_t) row]);
        const double azimuth = column == xColumn ? typed : current.azimuthDegrees;
        const double distance = column == yColumn ? std::max (0.0, typed) : current.distance;
        const auto p = fromPolar (azimuth, distance);
        const double limit = params::kCoordRange;
        const double nx = juce::jlimit (-limit, limit, p.x), ny = juce::jlimit (-limit, limit, p.y);
        lastAzimuth[(size_t) row] = wrapAzimuth (azimuth);

        switch (content)
        {
            case Content::Sources:
                setParameters (processor, { { params::sourceId (row, 'x'), (float) nx }, { params::sourceId (row, 'y'), (float) ny } });
                break;
            case Content::Speakers:
                setParameters (processor, { { params::speakerId (row, 'x'), (float) nx }, { params::speakerId (row, 'y'), (float) ny } });
                break;
            case Content::VirtualSpeakers:
            {
                auto list = processor.getManualVirtualSpeakers();
                list[(size_t) row].x = nx;
                list[(size_t) row].y = ny;
                processor.setManualVirtualSpeakers (list);
                break;
            }
        }

        return formatCell (column, getValue (row, column));
    }

    const bool isSpread = content == Content::Sources && column == spreadColumn;
    const bool isTrim = column == trimColumn && content != Content::VirtualSpeakers;
    std::optional<SpeakerColumn> extra;
    if (column >= extraColumn)
        if (const auto columns = extraColumns(); column - extraColumn < (int) columns.size())
            extra = columns[(size_t) (column - extraColumn)];
    const float limit = params::kCoordRange;
    const double value = isSpread ? juce::jlimit (0.0, (double) params::kMaxSpread, typed)
                       : isTrim   ? juce::jlimit ((double) params::kMinTrimDb, (double) params::kMaxTrimDb, typed)
                       : extra    ? juce::jlimit (extra->minimum, extra->maximum, typed)
                                  : juce::jlimit (-(double) limit, (double) limit, typed);

    switch (content)
    {
        case Content::Sources:
            if (column == xColumn)      setParameter (processor, params::sourceId (row, 'x'), (float) value);
            if (column == yColumn)      setParameter (processor, params::sourceId (row, 'y'), (float) value);
            if (column == spreadColumn) setParameter (processor, params::spreadId (row), (float) value);
            if (column == trimColumn)   setParameter (processor, params::sourceTrimId (row), (float) value);
            if (extra)                  setParameter (processor, extra->idFor (row), (float) value);
            break;

        case Content::Speakers:
            if (column == xColumn)      setParameter (processor, params::speakerId (row, 'x'), (float) value);
            if (column == yColumn)      setParameter (processor, params::speakerId (row, 'y'), (float) value);
            if (column == trimColumn)   setParameter (processor, params::speakerTrimId (row), (float) value);
            if (extra)                  setParameter (processor, extra->idFor (row), (float) value);
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

    return formatCell (column, getValue (row, column));
}

size_t PositionTable::signature() const
{
    const int rows = getRowCount();
    size_t seed = (size_t) rows + (processor.getCoordinateMode() == SpatialProcessor::CoordinateMode::Polar ? 1000003u : 0u);

    for (int row = 0; row < rows; ++row)
    {
        for (int column : { (int) xColumn, (int) yColumn, (int) spreadColumn, (int) trimColumn, (int) muteColumn, (int) soloColumn, (int) excludeColumn })
            hashCombine (seed, getValue (row, column));
        for (int k = 0; k < SpatialProcessor::kMaxSpeakerColumns; ++k)
            hashCombine (seed, getValue (row, extraColumn + k));
    }

    if (content == Content::Sources)
    {
        const auto groups = processor.getSourceGroups();
        for (int row = 0; row < rows; ++row)
            hashCombine (seed, (double) groups.groupOf (row));
    }

    return seed;
}

void PositionTable::refresh()
{
    if (processor.getCoordinateMode() != shownMode)
        updateCoordinateHeaders();

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
    if (column == groupColumn)
    {
        // The group's colour and number, or a dash for a source that is in no group.
        const auto groups = processor.getSourceGroups();
        const int id = groups.groupOf (row);
        const auto text = id > 0 ? juce::String (id) : juce::String ("-");
        if (id > 0)
        {
            g.setColour (FieldComponent::sourceColour (row, groups));
            g.fillEllipse (14.0f, ((float) height - 10.0f) * 0.5f, 10.0f, 10.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (id > 0 ? 0.9f : 0.4f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (text, 0, 0, width - 8, height, juce::Justification::centredRight);
        return;
    }

    if (column != indexColumn)
        return;

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String (row + 1), 0, 0, width - 8, height, juce::Justification::centredRight);
}

juce::Component* PositionTable::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
{
    if (column == indexColumn || column == groupColumn || row >= getRowCount())
    {
        delete existing;
        return nullptr;
    }

    if (column == muteColumn || column == soloColumn || column == excludeColumn)
    {
        auto* box = dynamic_cast<SwitchCell*> (existing);
        if (box == nullptr)
        {
            delete existing;
            box = new SwitchCell();
            box->onClick = [this, box] { setSwitch (box->row, box->column, box->getToggleState()); };
        }

        box->row = row;
        box->column = column;
        box->setToggleState (getValue (row, column) >= 0.5, juce::dontSendNotification);
        return box;
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
    cell->showValue (formatCell (column, getValue (row, column)));
    return cell;
}

//==============================================================================
PositionTableContent::PositionTableContent (SpatialProcessor& p, io::Kind k)
    : processor (p), kind (k)
{
    const bool sources = kind == io::Kind::Sources;

    countLabel.setText (sources ? juce::String ("Number of sources") : "Number of " + processor.getOutputNoun().toLowerCase() + "s", juce::dontSendNotification);
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

        processor.refreshLayout();
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

    // How positions are shown and typed, the same setting as in the settings window.
    coordinateBox.addItem ("Cartesian", 1);
    coordinateBox.addItem ("Polar", 2);
    coordinateBox.setTooltip ("How positions are shown and edited: Cartesian (x, y in metres) or Polar (azimuth in degrees, "
                              "counter-clockwise from the front, and distance in metres).");
    coordinateBox.setSelectedId (processor.getCoordinateMode() == SpatialProcessor::CoordinateMode::Polar ? 2 : 1, juce::dontSendNotification);
    coordinateBox.onChange = [this]
    {
        if (! syncing)
        {
            processor.setCoordinateMode (coordinateBox.getSelectedId() == 2 ? SpatialProcessor::CoordinateMode::Polar
                                                                            : SpatialProcessor::CoordinateMode::Cartesian);
            refresh();
        }
    };
    addAndMakeVisible (coordinateBox);

    // The plugin's action on all speakers (VecPan: place them on the reference circle), in the speakers window.
    if (! sources)
        if (const auto action = processor.getSpeakerAction())
        {
            actionButton = std::make_unique<juce::TextButton> (action->title);
            actionButton->setTooltip (action->tooltip);
            actionButton->onClick = [this, run = action->run]
            {
                run();
                refresh();
            };
            addAndMakeVisible (*actionButton);
        }

    // The room size, the same setting as in the settings window.
    roomLabel.setJustificationType (juce::Justification::centredRight);
    roomLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    addAndMakeVisible (roomLabel);
    roomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    roomSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
    roomSlider.setRange (params::kMinRoomHalf, params::kMaxRoomHalf, 0.5);
    roomSlider.setSkewFactorFromMidPoint (20.0);
    roomSlider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " m"; };
    roomSlider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
    roomSlider.setTooltip ("Half the width of the room in metres: how far the pad reaches. Positions typed here may lie beyond it (up to 100 m).");
    roomSlider.setValue (processor.getRoomHalfExtent(), juce::dontSendNotification);
    roomSlider.onValueChange = [this]
    {
        if (! syncing)
            processor.setRoomHalfExtent (roomSlider.getValue());
    };
    addAndMakeVisible (roomSlider);

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
    return kind == io::Kind::Sources ? params::kNumSourcesId : params::kNumSpeakersId;
}

int PositionTableContent::getPreferredWidth() const
{
    const int firstRow = (kind == io::Kind::Sources ? 130 + 110 : 130 + 110 + 16 + 110 + 90)   // count, (virtual speaker menu)
                       + 16 + kCoordinateBoxWidth;                                              // coordinates
    const int secondRow = 76 + 190 + (actionButton != nullptr ? 16 + kActionButtonWidth : 0);   // room size, (action)
    const int topBar = std::max (firstRow, secondRow);
    int tables = mainTable->getPreferredWidth();
    if (virtualTable != nullptr)
        tables += kGap + virtualTable->getPreferredWidth();

    return 2 * kMargin + std::max (topBar, tables);
}

int PositionTableContent::getHeightForRows (int rows) const
{
    return kMargin + 2 * kTopBarHeight + 8 + PositionTable::kCaptionHeight + PositionTable::kHeaderHeight
         + rows * PositionTable::kRowHeight + 2 + kMargin;
}

void PositionTableContent::refresh()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);
    bool layoutChanged = false;

    // The count: its range follows the channel count of the track.
    const bool sources = kind == io::Kind::Sources;
    const int channels = std::min (sources ? processor.getTotalNumInputChannels() : processor.getTotalNumOutputChannels(), tlos::kMaxChannels);
    const int maximum = std::max (channels, 1);
    const int inUse = sources ? processor.getActiveSources() : processor.getActiveSpeakers();
    countSlider.setEnabled (channels > 1);
    if (! juce::approximatelyEqual (countSlider.getMaximum(), (double) maximum))
        countSlider.setRange (1.0, (double) maximum, 1.0);
    if (! juce::approximatelyEqual (countSlider.getValue(), (double) inUse))
        countSlider.setValue ((double) inUse, juce::dontSendNotification);

    if (! juce::approximatelyEqual (roomSlider.getValue(), processor.getRoomHalfExtent()) && ! roomSlider.isMouseButtonDown())
        roomSlider.setValue (processor.getRoomHalfExtent(), juce::dontSendNotification);

    const int wantedCoordinates = processor.getCoordinateMode() == SpatialProcessor::CoordinateMode::Polar ? 2 : 1;
    if (coordinateBox.getSelectedId() != wantedCoordinates)
        coordinateBox.setSelectedId (wantedCoordinates, juce::dontSendNotification);

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
    if (kind == io::Kind::Speakers)
    {
        bar.removeFromLeft (16);
        virtualLabel.setBounds (bar.removeFromLeft (110));
        virtualModeBox.setBounds (bar.removeFromLeft (90).reduced (0, 4));
    }
    bar.removeFromLeft (16);
    coordinateBox.setBounds (bar.removeFromLeft (kCoordinateBoxWidth).reduced (0, 4));

    // Second row: the room size.
    bar = area.removeFromTop (kTopBarHeight);
    roomLabel.setBounds (bar.removeFromLeft (76));
    roomSlider.setBounds (bar.removeFromLeft (190).reduced (0, 4));
    if (actionButton != nullptr)
    {
        bar.removeFromLeft (16);
        actionButton->setBounds (bar.removeFromLeft (kActionButtonWidth).reduced (0, 4));
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
PositionTableWindow::PositionTableWindow (SpatialProcessor& processor, io::Kind kind)
    : juce::DocumentWindow (processor.getName() + (kind == io::Kind::Sources ? juce::String (" - Sources") : " - " + processor.getOutputNoun() + "s"),
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

std::optional<juce::Point<int>> PositionTableWindow::chooseBesidePosition (juce::Rectangle<int> mainWindow, juce::Rectangle<int> available,
                                                                          juce::Rectangle<int> size, juce::BorderSize<int> frame, int gap)
{
    const int totalWidth = size.getWidth() + frame.getLeftAndRight();
    const int totalHeight = size.getHeight() + frame.getTopAndBottom();

    // Level with the top of the main window, moved as little as needed to stay on the screen.
    const int minY = available.getY() + frame.getTop();
    const int maxY = available.getBottom() - size.getHeight() - frame.getBottom();
    if (maxY < minY || totalWidth > available.getWidth() || totalHeight > available.getHeight())
        return std::nullopt;

    const int y = juce::jlimit (minY, maxY, mainWindow.getY());

    const int rightX = mainWindow.getRight() + gap + frame.getLeft();
    if (rightX + size.getWidth() + frame.getRight() <= available.getRight())
        return juce::Point<int> (rightX, y);

    const int leftX = mainWindow.getX() - gap - frame.getRight() - size.getWidth();
    if (leftX - frame.getLeft() >= available.getX())
        return juce::Point<int> (leftX, y);

    return std::nullopt;
}

void PositionTableWindow::show (juce::Component* around)
{
    if (! isVisible() && around != nullptr)
    {
        // Beside the plug-in window if the screen has room, otherwise over it.
        const auto mainBounds = around->getTopLevelComponent()->getScreenBounds();
        juce::Rectangle<int> available = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                                             ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt()
                                             : juce::Rectangle<int>();
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (mainBounds))
            available = display->userBounds.toNearestInt();

        juce::BorderSize<int> frame;
        if (auto* peer = getPeer())
            frame = peer->getFrameSize();

        if (const auto position = chooseBesidePosition (mainBounds, available, getLocalBounds(), frame, 8))
            setTopLeftPosition (*position);
        else
            centreAroundComponent (around, getWidth(), getHeight());
    }

    setAlwaysOnTop (true);
    setVisible (true);
    applyLimits(); // the native frame size is known once the window is shown
    toFront (true);
}
}
