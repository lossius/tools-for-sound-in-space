// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PositionTableWindow.h
    @brief Windows with tables of the source, speaker and virtual speaker positions, which can be edited. */

#pragma once

#include "PluginProcessor.h"
#include "PositionIO.h"

#include <functional>
#include <memory>

/** A table of positions with a caption and a header that stays visible while the rows scroll.

    Shows one of: the sources in use (x, y, spread), the speakers in use (x, y) or the manual virtual
    speakers (x, y). The number cells can be edited: click one, type a value and press Return. Values
    that are not numbers are rejected, others are limited to the allowed range. Edits go through the
    parameters (with change gestures), so hosts record them. Message thread only. */
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

    /** Column IDs, which are also the order of the columns. */
    enum Column
    {
        indexColumn = 1,  ///< Row number (1-based), not editable.
        xColumn,          ///< X position in metres.
        yColumn,          ///< Y position in metres.
        spreadColumn      ///< Spread in metres (sources only).
    };

    static constexpr int kRowHeight = 24;       ///< Height of a row in pixels.
    static constexpr int kHeaderHeight = 26;    ///< Height of the header in pixels.
    static constexpr int kCaptionHeight = 22;   ///< Height of the caption above the table in pixels.
    static constexpr int kScrollBarWidth = 12;  ///< Width of the vertical scroll bar in pixels.

    /** Creates the table. @param processor  The processor whose parameters are shown and edited.
        @param content  Which positions the table shows. */
    PositionTable (TriPanAudioProcessor& processor, Content content);

    /** Detaches the model from the table. */
    ~PositionTable() override;

    /** @return The width in pixels that shows all columns and the scroll bar. */
    int getPreferredWidth() const noexcept;

    /** @return The number of rows: the sources or speakers in use, or the manual virtual speakers. */
    int getRowCount() const;

    /** @return The value in a cell in metres. @param row  Zero-based row. @param column  xColumn, yColumn or spreadColumn.
        Returns 0 for cells that do not exist. */
    double getValue (int row, int column) const;

    /** Sets a cell from text typed by the user.
        @param row     Zero-based row.
        @param column  xColumn, yColumn or spreadColumn.
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
    /** Formats a value in metres for display (3 decimals, no negative zero). */
    static juce::String format (double metres);

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

    TriPanAudioProcessor& processor;   ///< The processor whose parameters are shown and edited.
    Content content;                   ///< What the rows are.
    juce::Label caption;               ///< Title above the table.
    juce::TableListBox table;          ///< The table itself.
    size_t lastSignature = 0;          ///< Signature at the last refresh().

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PositionTable)
};

/** The content of a table window: the controls on top (the number of sources or speakers and, for
    speakers, the virtual speaker mode) and the table or tables below.

    The speakers window shows a second table, to the right, with the manual virtual speakers while they
    can be edited by hand (mode Manual and the speakers set to Edit). Message thread only. */
class PositionTableContent final : public juce::Component,
                                   private juce::Timer
{
public:
    static constexpr int kMargin = 12;      ///< Space around the content in pixels.
    static constexpr int kTopBarHeight = 32; ///< Height of the row of controls in pixels.
    static constexpr int kMinVisibleRows = 4; ///< The window can not be made smaller than this many rows.

    /** Creates the content and starts a 10 Hz timer that follows the parameters.
        @param processor  The processor whose parameters are shown and edited.
        @param kind       Sources (x, y, spread) or speakers (x, y). */
    PositionTableContent (TriPanAudioProcessor& processor, tripan::io::Kind kind);

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

    TriPanAudioProcessor& processor;   ///< The processor whose parameters are shown and edited.
    tripan::io::Kind kind;             ///< Sources or speakers.

    juce::Label countLabel;            ///< "Number of sources" / "Number of speakers".
    juce::Slider countSlider;          ///< How many are in use.
    juce::Label virtualLabel { {}, "Virtual speakers" };   ///< Caption of the virtual speaker menu (speakers window).
    juce::ComboBox virtualModeBox;     ///< Auto / Manual / Off (speakers window).

    std::unique_ptr<PositionTable> mainTable;      ///< Sources or speakers.
    std::unique_ptr<PositionTable> virtualTable;   ///< Manual virtual speakers, only while they can be edited.

    int lastRows = 0;          ///< Row count at the last refresh().
    bool syncing = false;      ///< True while the controls are updated from the parameters.
    bool draggingCount = false; ///< True while the count slider is dragged.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PositionTableContent)
};

/** A window with the table(s) for sources or speakers.

    The window is as wide as its content needs and follows when the width changes (the virtual speaker
    table appears or disappears). It can be resized vertically between a few rows and every row, and
    never beyond the screen; with more rows than fit, the table scrolls. Closing the window only hides
    it. */
class PositionTableWindow final : public juce::DocumentWindow
{
public:
    /** Creates the window (hidden).
        @param processor  The processor whose parameters are shown and edited.
        @param kind       Sources or speakers. */
    PositionTableWindow (TriPanAudioProcessor& processor, tripan::io::Kind kind);

    /** Hides the window. */
    void closeButtonPressed() override { setVisible (false); }

    /** Shows the window in front, the first time centred over `around`.
        @param around  Component to centre over on first use; may be null. */
    void show (juce::Component* around);

    /** @return The content, for the tests. */
    PositionTableContent& getContent() noexcept { return *content; }

    /** Sets the size limits from the content: fixed width, height between a few rows and every row (limited to the screen),
        and keeps the current size inside them. The limits include the native title bar. Called when the content's layout changes.
        @param initial  True for the first call, which also picks the starting height. */
    void applyLimits (bool initial = false);

private:
    PositionTableContent* content = nullptr;   ///< Owned by the window (set as its content component).
    bool shownBefore = false;                  ///< Whether show() has positioned the window already.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PositionTableWindow)
};
