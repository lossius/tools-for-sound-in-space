// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file SpatialEditor.h
    @brief The plugin window shared by all plugins: count sliders, Edit/View and virtual speaker menus, file buttons and the pad. */

#pragma once

#include "tlos/FieldComponent.h"
#include "tlos/SpatialProcessor.h"
#include "tlos/PositionIO.h"
#include "tlos/PositionTableWindow.h"

namespace tlos
{

/** The editor window of every plugin in this repository, so that they all look and work alike.

    All controls live in a child component laid out at the base size (kBaseWidth x kBaseHeight) and
    scaled uniformly with the window, which has a fixed aspect ratio. Rows from the top: sources,
    speakers, display options (an optional overlay toggle, links, reset, zoom, groups), the virtual speaker
    menu (only if the processor supports them) with the outside rolloff slider, the room size slider with the
    status text, then (for a plugin that has controls of its own, `extraRows`) the row of those controls, and
    the FieldComponent pad and a footer.

    A plugin derives from this class and passes its own FieldComponent (which draws what is specific to
    its panning method) and the text of the toggle that shows or hides that drawing. */
class SpatialEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    /** Creates the editor for `processor` and restores the saved window size.
        @param processor            The processor the editor belongs to.
        @param padComponent         The pad, usually a subclass of FieldComponent; the editor owns it.
        @param overlayToggleText    Text of the toggle for FieldComponent::setShowOverlay() (for example
                                    "Show triangulation"), or empty for no toggle.
        @param overlayToggleTip     Tooltip of that toggle.
        @param extraRows            Rows of controls of the plugin's own (see layoutExtraControls()); each makes the
                                    window 32 pixels higher at the base size. */
    SpatialEditor (SpatialProcessor& processor, std::unique_ptr<FieldComponent> padComponent,
                   const juce::String& overlayToggleText, const juce::String& overlayToggleTip = {}, int extraRows = 0);

    /** Stops the timer and detaches from the parameters. */
    ~SpatialEditor() override;

    /** The GUI is laid out at this size and scaled uniformly (with a fixed aspect ratio) when the
        window is resized. The base size is also the smallest allowed size. */
    static constexpr int kBaseWidth = 700;   ///< Width in pixels at scale 1.
    static constexpr int kBaseHeight = 892;  ///< Height in pixels at scale 1 for a window without extra rows.
    static constexpr int kMaxScale = 3;      ///< The window can grow to this multiple of the base size.

    /** Fills the background. */
    void paint (juce::Graphics&) override;

    /** Applies the scale transform to the content and remembers the window width. */
    void resized() override;

    /** @return The height in pixels at scale 1: kBaseHeight plus 32 for every extra row. */
    int getBaseHeight() const noexcept { return baseHeight; }

    /** Current display scale (1.0 at the base size). */
    float getScale() const noexcept { return scale; }

    /// @name Access to the controls (used by the tests)
    /// @{
    FieldComponent& getField() noexcept { return *field; }                             ///< The pad.
    juce::Slider& getSourceCountSlider() noexcept { return sourceCount; }              ///< "Number of Sources" slider.
    juce::Slider& getSpeakerCountSlider() noexcept { return speakerCount; }            ///< "Number of Speakers" slider.
    juce::ComboBox& getSourcesModeBox() noexcept { return sourcesModeBox; }            ///< Edit/View menu for sources.
    juce::ComboBox& getSpeakersModeBox() noexcept { return speakersModeBox; }          ///< Edit/View menu for speakers.
    juce::ComboBox& getVirtualModeBox() noexcept { return virtualModeBox; }            ///< Auto/Manual/Off menu.
    juce::Slider& getRoomSlider() noexcept { return roomSlider; }                      ///< "Room size" slider.
    juce::Slider& getOutsideSlider() noexcept { return outsideSlider; }                ///< "Outside rolloff" slider.
    juce::ToggleButton& getOverlayButton() noexcept { return overlayButton; }          ///< The overlay toggle (e.g. "Show triangulation").
    juce::ToggleButton& getShowLinksButton() noexcept { return showLinksButton; }      ///< "Show links".
    juce::TextButton& getResetButton() noexcept { return resetButton; }                ///< "Reset layout".
    juce::TextButton& getZoomInButton() noexcept { return zoomInButton; }              ///< "Zoom In".
    juce::TextButton& getZoomOutButton() noexcept { return zoomOutButton; }            ///< "Zoom Out".
    juce::TextButton& getGroupButton() noexcept { return groupButton; }                ///< "Group".
    juce::TextButton& getUngroupButton() noexcept { return ungroupButton; }            ///< "Ungroup".
    juce::TextButton& getExportSourcesButton() noexcept { return exportSourcesButton; }   ///< "Export sources...".
    juce::TextButton& getImportSourcesButton() noexcept { return importSourcesButton; }   ///< "Import sources...".
    juce::TextButton& getExportSpeakersButton() noexcept { return exportSpeakersButton; } ///< "Export speakers...".
    juce::TextButton& getImportSpeakersButton() noexcept { return importSpeakersButton; } ///< "Import speakers...".
    juce::TextButton& getSourcesTableButton() noexcept { return sourcesTableButton; }     ///< Opens the sources table window.
    juce::TextButton& getSpeakersTableButton() noexcept { return speakersTableButton; }   ///< Opens the speakers table window.
    /// @}

    /** Writes / reads a positions file and shows the outcome in the status text. The buttons call
        these after the user has chosen a file; failures are returned, not shown in a dialog. */
    juce::Result exportPositionsTo (io::Kind kind, const juce::File& file);
    /** @copydoc exportPositionsTo() */
    juce::Result importPositionsFrom (io::Kind kind, const juce::File& file);

    /** Opens (or brings to the front) the window with the table of source or speaker positions.
        @return The window, which is owned by the editor and closed with it. */
    PositionTableWindow& showTableWindow (io::Kind kind);

    /** What the table buttons do: closes (hides) the window of that kind if it is open, otherwise opens it. */
    void toggleTableWindow (io::Kind kind);

    /** @return The table window of that kind, or nullptr if it has not been opened yet. */
    PositionTableWindow* getTableWindow (io::Kind kind) noexcept;

    /** Re-reads the parameters and channel counts into the controls (also called by a timer). */
    void syncControls();

protected:
    /** Hook for the controls of the plugin's panning method (for example a rolloff slider): positions them
        in the extra row (base-size pixels; `row` is the whole row). Only called if the editor was created with
        extra rows. Add the controls themselves with addToContent() in the constructor. The default has none. */
    virtual void layoutExtraControls (juce::Rectangle<int>& row);

    /** Adds a child to the area that is laid out at the base size and scaled with the window. */
    void addToContent (juce::Component& child) { content.addAndMakeVisible (child); }

    /** Positions every control again (a subclass calls this at the end of its constructor, after it has added
        its own controls: the base class has laid out the window before they existed). */
    void relayout() { layoutContent(); }

private:
    /** Timer callback: keeps the controls in step with the parameters. */
    void timerCallback() override { syncControls(); }

    /** Positions every control in base-size pixels. */
    void layoutContent();

    /** Asks where to save (async file dialog) and exports. */
    void chooseFileAndExport (io::Kind kind);

    /** Asks which file to open (async file dialog) and imports. */
    void chooseFileAndImport (io::Kind kind);

    /** Shows a message in the status area for a few seconds. */
    void showStatus (const juce::String& message);

    /** Wires a count slider to its parameter. The slider shows the effective value (parameter
        clamped to the track's channel count) and its maximum is the channel count. */
    void setUpCountSlider (juce::Slider& slider, const juce::String& parameterId);

    /** Shows `effectiveValue` and the channel count as the slider's maximum, without changing the parameter. */
    void syncCountSlider (juce::Slider& slider, int effectiveValue, int channels);

    SpatialProcessor& owner; ///< The processor this editor belongs to.

    juce::TooltipWindow tooltipWindow { this, 600 }; ///< Shows the tooltips of the buttons, menus and the pad.

    // All controls live in `content`, which is laid out at the base size and scaled by a transform.
    // (Declared before the controls so that it is destroyed after them.)
    juce::Component content;       ///< Parent of all controls, laid out at the base size.
    int baseHeight = kBaseHeight;  ///< Height at scale 1, see getBaseHeight().
    int extraRowCount = 0;         ///< Rows of controls of the plugin's own.
    float scale = 1.0f;            ///< Current display scale, see getScale().
    bool sizeIsRestored = false;   ///< True once the saved window size has been applied.

    std::unique_ptr<FieldComponent> field; ///< The XY pad.

    juce::Label sourcesLabel  { {}, "Sources" };    ///< Row 1 caption.
    juce::Label speakersLabel { {}, "Speakers" };   ///< Row 2 caption.
    juce::Slider sourceCount;      ///< Number of sources in use.
    juce::Slider speakerCount;     ///< Number of speakers in use.
    juce::Label infoLabel;         ///< Status text and the track's channel counts.
    juce::ComboBox sourcesModeBox;   ///< "Edit" / "View" for sources.
    juce::ComboBox speakersModeBox;  ///< "Edit" / "View" for speakers.
    juce::Label virtualLabel { {}, "Virtual speakers" }; ///< Caption of the virtual speaker menu.
    juce::ComboBox virtualModeBox;                       ///< Auto / Manual / Off (only if the processor supports virtual speakers).
    juce::Label roomLabel { {}, "Room size" };           ///< Caption of the room size slider.
    juce::Slider roomSlider;                             ///< Half the width of the room in metres (see SpatialProcessor::getRoomHalfExtent()).
    juce::Label outsideLabel { {}, "Outside rolloff" };  ///< Caption of the outside rolloff slider.
    juce::Slider outsideSlider;                          ///< Level drop in dB per doubling of the distance outside the speakers.
    std::unique_ptr<juce::SliderParameterAttachment> outsideAttachment; ///< Ties the slider to its parameter.
    juce::ToggleButton overlayButton;                    ///< Shows or hides the plugin's overlay on the pad (e.g. the triangles).
    juce::ToggleButton showLinksButton { "Show links" }; ///< Draws the lines from each source to the speakers it feeds.
    juce::TextButton resetButton { "Reset layout" };     ///< Puts the editable kinds back on rings.
    juce::TextButton zoomInButton { "Zoom In" };         ///< Zooms the pad towards the speakers.
    juce::TextButton zoomOutButton { "Zoom Out" };       ///< Zooms the pad back out.
    juce::TextButton groupButton { "Group" };            ///< Groups the selected sources.
    juce::TextButton ungroupButton { "Ungroup" };        ///< Dissolves the groups of the selected sources.
    juce::TextButton exportSourcesButton  { "Export sources..." };   ///< Saves source positions.
    juce::TextButton importSourcesButton  { "Import sources..." };   ///< Loads source positions.
    juce::TextButton exportSpeakersButton { "Export speakers..." };  ///< Saves speaker positions.
    juce::TextButton importSpeakersButton { "Import speakers..." };  ///< Loads speaker positions.
    juce::TextButton sourcesTableButton  { "Table..." };   ///< Opens the table of source positions.
    juce::TextButton speakersTableButton { "Table..." };   ///< Opens the table of speaker positions.

    juce::HyperlinkButton authorLink { "Trond Lossius", juce::URL ("https://trondlossius.no") };   ///< Footer, left: the author's site.
    juce::HyperlinkButton helpLink { "Help", juce::URL ("https://trondlossius.no/software/") };      ///< Footer, centre: help and software page.
    juce::HyperlinkButton institutionLink { "FTVS, INN", juce::URL ("https://www.inn.no/english/about-inn/faculty-of-film-tv-games/") }; ///< Footer, right: the faculty.

    std::unique_ptr<PositionTableWindow> sourcesTableWindow;   ///< Table of source positions, created on first use.
    std::unique_ptr<PositionTableWindow> speakersTableWindow;  ///< Table of speaker positions, created on first use.

    std::unique_ptr<juce::FileChooser> fileChooser; ///< Kept alive while a dialog is open.
    juce::String statusText;                        ///< Message currently shown in the status area.
    juce::uint32 statusUntil = 0;                   ///< Millisecond counter value at which the message expires.

    bool syncing = false;   ///< True while controls are updated from parameters (ignores their callbacks).
    bool dragging = false;  ///< True while the user drags a count slider, so that syncing does not move it.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialEditor)
};
}
