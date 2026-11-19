// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PositionTableWindow.h
    @brief Windows with tables of the source, speaker and virtual speaker positions, which can be edited. */

#pragma once

#include "tlos/SpatialProcessor.h"
#include "tlos/PositionIO.h"

#include <functional>
#include <memory>
#include <optional>

namespace tlos
{

/** A table of positions with a caption and a header that stays visible while the rows scroll.

    Shows one of: the sources in use (x, y, spread, group, trim, mute, solo), the speakers in use (x, y, trim, mute,
    solo, exclude) or the manual virtual speakers (x, y). The number cells can be edited: click one, type a value
    and press Return. Values that are not numbers are rejected, others are limited to the allowed range. The
    mute, solo and exclude cells are check boxes. Edits go through the parameters (with change gestures), so
    hosts record them; exclude is saved with the session instead. Message thread only. */
class PositionTable final : public juce::Component,
                            private juce::TableListBoxModel
{
public:
    /** What the rows of the table are. */
    enum class Content
    {
        Sources,          ///< The sources in use: x, y and spread.
        Speakers,         ///< The speakers in use: x and y.
        VirtualSpeakers   ///< The manual virtual speakers: x and y.
    };

    /** Column IDs. The order on screen is: #, X, Y, Spread, the plugin's own columns, Trim, Exclude, Mute, Solo, Group. */
    enum Column
    {
        indexColumn = 1,  ///< Row number (1-based), not editable.
        xColumn,          ///< X position in metres; in polar mode the azimuth in degrees.
        yColumn,          ///< Y position in metres; in polar mode the distance in metres.
        spreadColumn,     ///< Spread in metres (sources only).
        groupColumn,      ///< Group number of the source (sources only), not editable here: groups are made on the pad.
        trimColumn,       ///< Trim in dB (sources and speakers).
        muteColumn,       ///< Mute switch, a check box (sources and speakers).
        soloColumn,       ///< Solo switch, a check box (sources and speakers).
        excludeColumn,    ///< Exclude from panning, a check box (speakers only).
        extraColumn       ///< The plugin's own per-channel parameters (SpatialProcessor::getSourceColumns() / getSpeakerColumns()): extraColumn + k is the k-th (zero-based).
    };

    static constexpr int kRowHeight = 24;       ///< Height of a row in pixels.
    static constexpr int kHeaderHeight = 26;    ///< Height of the header in pixels.
    static constexpr int kCaptionHeight = 22;   ///< Height of the caption above the table in pixels.
    static constexpr int kScrollBarWidth = 12;  ///< Width of the vertical scroll bar in pixels.
    static constexpr int kGroupColumnWidth = 70; ///< Width of the group column in pixels.
    static constexpr int kSwitchColumnWidth = 52; ///< Width of the mute, solo and exclude columns in pixels.

    /** Creates the table. @param processor  The processor whose parameters are shown and edited.
        @param content  Which positions the table shows. */
    PositionTable (SpatialProcessor& processor, Content content);

    /** Detaches the model from the table. */
    ~PositionTable() override;

    /** @return The width in pixels that shows all columns and the scroll bar. */
    int getPreferredWidth() const noexcept;

    /** @return The number of rows: the sources or speakers in use, or the manual virtual speakers. */
    int getRowCount() const;

    /** @return The value in a cell: metres for xColumn, yColumn and spreadColumn (in polar mode degrees for
        xColumn and metres for yColumn, see SpatialProcessor::getCoordinateMode()), dB for trimColumn, and 0 or 1 for
        the switch columns. @param row  Zero-based row. @param column  The column. Returns 0 for cells that do not exist. */
    double getValue (int row, int column) const;

    /** Sets a cell from text typed by the user.
        @param row     Zero-based row.
        @param column  xColumn, yColumn, spreadColumn or trimColumn. In polar mode xColumn is the azimuth (wrapped to
                       -180..180) and yColumn the distance; the position keeps the other one and is limited to +-100 m
                       on each axis. A source at the origin has no azimuth, so the last one shown is used.
        @param text    The text; it must be a plain number, otherwise the cell is left alone.
        @return        The text to show in the cell afterwards: the new value, limited to the allowed range
                       and rounded to 3 decimals, or the old value if the text was rejected. */
    juce::String setValueFromText (int row, int column, const juce::String& text);

    /** @return The table component, for the tests. */
    juce::TableListBox& getTable() noexcept { return table; }

    /** Updates the rows if any value or the number of rows changed (the owner calls this by timer).
        A cell that is being edited is not touched. */
    void refresh();

    /** Lays out the caption and the table. */
    void resized() override;

private:
    /** @return The plugin's own per-channel columns of this table (none for the virtual speakers). */
    std::vector<SpeakerColumn> extraColumns() const;

    /** @return The Cartesian position of a row (zero-based, in range), read from the parameters or the virtual speaker list. */
    Vec2 positionOf (int row) const;

    /** Sets the header texts of the x and y columns for the current coordinate mode. */
    void updateCoordinateHeaders();

    /** Formats a value in metres for display (3 decimals, no negative zero). */
    static juce::String format (double metres);

    /** Formats the value of a number cell for display: a trim in dB with one decimal, other columns as format(). */
    juce::String formatCell (int column, double value) const;

    /** Sets a switch cell (mute, solo or exclude) of a row. */
    void setSwitch (int row, int column, bool on);

    /** @return A number that changes when any displayed value or the row count changes. */
    size_t signature() const;

    /** @return The number of rows (TableListBoxModel). */
    int getNumRows() override { return getRowCount(); }

    /** Draws the background of a row (alternating shades). */
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;

    /** Draws the row number. */
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;

    /** Supplies the editable component for a value cell. */
    juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;

    SpatialProcessor& processor;   ///< The processor whose parameters are shown and edited.
    Content content;                   ///< What the rows are.
    juce::Label caption;               ///< Title above the table.
    juce::TableListBox table;          ///< The table itself.
    size_t lastSignature = 0;          ///< Signature at the last refresh().
    SpatialProcessor::CoordinateMode shownMode = SpatialProcessor::CoordinateMode::Cartesian;   ///< Mode the headers show.
    mutable std::array<double, kMaxChannels> lastAzimuth {};   ///< Last azimuth shown per row, used where a point is at the origin.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PositionTable)
};

/** The content of a table window: the controls on top (first row: the number of sources or speakers and, for
    speakers, the virtual speaker mode, and the coordinate mode; second row: the room size and, in the speakers window, the plugin's action
    on all speakers if it has one) and the table or tables below.

    The speakers window shows a second table, to the right, with the manual virtual speakers while they
    can be edited by hand (mode Manual and the speakers set to Edit). Message thread only. */
class PositionTableContent final : public juce::Component,
                                   private juce::Timer
{
public:
    static constexpr int kMargin = 12;      ///< Space around the content in pixels.
    static constexpr int kTopBarHeight = 32; ///< Height of the row of controls in pixels.
    static constexpr int kCoordinateBoxWidth = 100;   ///< Width of the coordinates menu in pixels.
    static constexpr int kActionButtonWidth = 170;    ///< Width of the speaker action button in pixels.
    static constexpr int kMinVisibleRows = 4; ///< The window can not be made smaller than this many rows.

    /** Creates the content and starts a 10 Hz timer that follows the parameters.
        @param processor  The processor whose parameters are shown and edited.
        @param kind       Sources (x, y, spread) or speakers (x, y). */
    PositionTableContent (SpatialProcessor& processor, io::Kind kind);

    /** Stops the timer. */
    ~PositionTableContent() override;

    /** @return The width in pixels that fits the controls and all tables currently shown. */
    int getPreferredWidth() const;

    /** @return The height in pixels with room for `rows` rows. */
    int getHeightForRows (int rows) const;

    /** @return The smallest sensible height: a few rows. */
    int getMinimumHeight() const { return getHeightForRows (kMinVisibleRows); }

    /** @return The largest sensible height: room for every row, so that nothing needs scrolling. */
    int getMaximumHeight() const { return getHeightForRows (std::max (lastRows, kMinVisibleRows)); }

    /** @return The most rows any shown table has. */
    int getRowCount() const { return lastRows; }

    /** Called when the preferred width or the number of rows changed, so that the window can adapt. */
    std::function<void()> onLayoutChanged;

    /// @name Access to the parts (used by the tests)
    /// @{
    PositionTable& getMainTable() noexcept { return *mainTable; }                    ///< The sources or speakers table.
    PositionTable* getVirtualTable() noexcept { return virtualTable.get(); }         ///< The virtual speaker table, or nullptr while it is hidden.
    juce::Slider& getCountSlider() noexcept { return countSlider; }                  ///< Number of sources or speakers.
    juce::ComboBox& getVirtualModeBox() noexcept { return virtualModeBox; }          ///< Auto / Manual / Off (speakers window).
    juce::Slider& getRoomSlider() noexcept { return roomSlider; }                    ///< Room size (the same setting as in the settings window).
    juce::TextButton* getActionButton() noexcept { return actionButton.get(); }      ///< The plugin's speaker action (speakers window), or nullptr.
    juce::ComboBox& getCoordinateBox() noexcept { return coordinateBox; }            ///< Cartesian / Polar (the same setting as in the settings window).
    /// @}

    /** Brings the controls and tables up to date with the parameters, and adds or removes the virtual
        speaker table. Also called by the timer. */
    void refresh();

    /** Lays out the controls and tables. */
    void resized() override;

    /** Fills the background. */
    void paint (juce::Graphics&) override;

private:
    /** Timer callback. */
    void timerCallback() override { refresh(); }

    /** @return The parameter ID of the count (numSources or numSpeakers). */
    const juce::String& countParameterId() const;

    SpatialProcessor& processor;   ///< The processor whose parameters are shown and edited.
    io::Kind kind;             ///< Sources or speakers.

    juce::Label countLabel;            ///< "Number of sources" / "Number of speakers".
    juce::Slider countSlider;          ///< How many are in use.
    juce::Label virtualLabel { {}, "Virtual speakers" };   ///< Caption of the virtual speaker menu (speakers window of a plugin that has virtual speakers).
    juce::ComboBox virtualModeBox;     ///< Auto / Manual / Off (speakers window).
    juce::ComboBox coordinateBox;      ///< Cartesian / Polar (SpatialProcessor::getCoordinateMode()).
    std::unique_ptr<juce::TextButton> actionButton;   ///< The plugin's action on all speakers (SpatialProcessor::getSpeakerAction()), speakers window only.
    juce::Label roomLabel { {}, "Room size" };   ///< Caption of the room size slider.
    juce::Slider roomSlider;           ///< Half the width of the room in metres (SpatialProcessor::getRoomHalfExtent()).

    std::unique_ptr<PositionTable> mainTable;      ///< Sources or speakers.
    std::unique_ptr<PositionTable> virtualTable;   ///< Manual virtual speakers, only while they can be edited.

    int lastRows = 0;          ///< Row count at the last refresh().
    bool syncing = false;      ///< True while the controls are updated from the parameters.
    bool hasVirtualMenu = false;   ///< Whether this window has the virtual speaker menu (the speakers window of a plugin that has virtual speakers).
    bool draggingCount = false; ///< True while the count slider is dragged.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PositionTableContent)
};

/** A window with the table(s) for sources or speakers.

    The window is as wide as its content needs and follows when the width changes (the virtual speaker
    table appears or disappears). It can be resized vertically between a few rows and every row, and
    never beyond the screen; with more rows than fit, the table scrolls. Closing the window only hides
    it. The window floats above ordinary windows, so it opens in front of the plug-in window (which hosts
    usually show as a floating window). */
class PositionTableWindow final : public juce::DocumentWindow
{
public:
    /** Creates the window (hidden).
        @param processor  The processor whose parameters are shown and edited.
        @param kind       Sources or speakers. */
    PositionTableWindow (SpatialProcessor& processor, io::Kind kind);

    /** Hides the window. */
    void closeButtonPressed() override { setVisible (false); }

    /** Shows the window in front of the plug-in window. A window that is not showing yet is placed beside
        `around` (to the right, otherwise to the left) when the screen has room, so that both can be seen at
        once, and otherwise centred over it. A window that is already showing is only brought to the front.
        @param around  The plug-in window; may be null (then the window stays where it is). */
    void show (juce::Component* around);

    /** Where to put a window of the given size so that it sits beside the main window without covering it.
        Prefers the right side, then the left; its top is level with the main window's, moved up or down as
        far as needed to stay on the screen.
        @param mainWindow  Screen bounds of the main window.
        @param available   Usable screen area (without menu bar and dock).
        @param size        Size of the window to place (without frame).
        @param frame       Size of the window frame (native title bar and borders), around `size`.
        @param gap         Space between the two windows.
        @return            The top-left of the window (its content area), or nothing if it fits on neither side. */
    static std::optional<juce::Point<int>> chooseBesidePosition (juce::Rectangle<int> mainWindow, juce::Rectangle<int> available,
                                                                 juce::Rectangle<int> size, juce::BorderSize<int> frame, int gap);

    /** @return The content, for the tests. */
    PositionTableContent& getContent() noexcept { return *content; }

    /** Sets the size limits from the content: fixed width, height between a few rows and every row (limited to the screen),
        and keeps the current size inside them. The limits include the native title bar. Called when the content's layout changes.
        @param initial  True for the first call, which also picks the starting height. */
    void applyLimits (bool initial = false);

private:
    PositionTableContent* content = nullptr;   ///< Owned by the window (set as its content component).

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PositionTableWindow)
};
}
