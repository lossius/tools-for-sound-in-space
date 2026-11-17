// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file SettingsWindow.h
    @brief The settings window: room size, level outside the speakers, virtual speakers, the plugin's own settings and OSC. */

#pragma once

#include "tlos/OscSettingsPanel.h"
#include "tlos/SpatialProcessor.h"

#include <vector>

namespace tlos
{
/** Moves a window beside the plug-in window if the screen has room (to the right, otherwise to the left),
    and otherwise centres it over it. Does nothing if `around` is null. Used for the table and settings windows
    the first time they are shown.
    @param window  The window to place; it should be showing already so that its frame size is known.
    @param around  The plug-in window or a component in it. */
void placeBeside (juce::TopLevelWindow& window, juce::Component* around);

/** What the settings window shows: sections of labelled controls, from the top

    - **Room**: the room size slider (see SpatialProcessor::getRoomHalfExtent()).
    - **Panning**: the outside rolloff slider, the virtual speaker menu (only for a plugin that has virtual
      speakers) and the rows the plugin adds with addRow() (DistPan: rolloff and blur).
    - **OSC**: the OscSettingsPanel.

    It follows the parameters and settings while it is showing (10 Hz). Message thread only. */
class SettingsContent final : public juce::Component,
                              private juce::Timer
{
public:
    static constexpr int kWidth = OscSettingsPanel::kWidth + 24;  ///< Width of the content in pixels.
    static constexpr int kRowHeight = 30;                         ///< Height of a row of controls in pixels.
    static constexpr int kLabelWidth = 150;                       ///< Width of the captions in pixels.

    /** Creates the content for `processor`. */
    explicit SettingsContent (SpatialProcessor& processor);

    /** Stops the timer. */
    ~SettingsContent() override;

    /** Adds a row of the plugin's own to the Panning section. The control belongs to the caller, which must keep it
        alive as long as the content exists; the content only shows it.
        @param caption  The text to the left of the control.
        @param control  The control, for example a slider. */
    void addRow (const juce::String& caption, juce::Component& control);

    /** @return The height in pixels that shows everything. */
    int getPreferredHeight() const noexcept { return preferredHeight; }

    /** Called when the preferred height changed (a row was added), so that the window can adapt. */
    std::function<void()> onSizeChanged;

    /// @name Access to the controls (used by the tests)
    /// @{
    juce::Slider& getRoomSlider() noexcept { return roomSlider; }              ///< Room size.
    juce::Slider& getOutsideSlider() noexcept { return outsideSlider; }        ///< Outside rolloff.
    juce::ComboBox& getVirtualModeBox() noexcept { return virtualModeBox; }    ///< Auto / Manual / Off (plugins with virtual speakers).
    OscSettingsPanel& getOscPanel() noexcept { return oscPanel; }              ///< The OSC section.
    /// @}

    /** Brings the controls up to date with the parameters and settings (the timer does this at 10 Hz). */
    void refresh();

    /** Lays out the sections. */
    void resized() override;

    /** Fills the background. */
    void paint (juce::Graphics&) override;

private:
    /** A row: a caption and a control. */
    struct Row
    {
        juce::Label* caption;       ///< The caption.
        juce::Component* control;   ///< The control.
    };

    /** Timer callback. */
    void timerCallback() override { if (isShowing()) refresh(); }

    /** Works out the rows and the height (and tells the owner if the height changed). */
    void updateLayoutData();

    SpatialProcessor& processor;           ///< The processor whose settings are shown.

    juce::Label roomHeader { {}, "Room" };                 ///< Header of the Room section.
    juce::Label roomLabel { {}, "Room size" };             ///< Caption of the room size slider.
    juce::Slider roomSlider;                               ///< Half the width of the room in metres.
    juce::Label panningHeader { {}, "Panning" };           ///< Header of the Panning section.
    juce::Label outsideLabel { {}, "Outside rolloff" };    ///< Caption of the outside rolloff slider.
    juce::Slider outsideSlider;                            ///< Level drop in dB per doubling of the distance outside the speakers.
    std::unique_ptr<juce::SliderParameterAttachment> outsideAttachment;  ///< Ties the slider to its parameter.
    juce::Label virtualLabel { {}, "Virtual speakers" };   ///< Caption of the virtual speaker menu.
    juce::ComboBox virtualModeBox;                         ///< Auto / Manual / Off.
    juce::Label oscHeader { {}, "OSC" };                   ///< Header of the OSC section.
    OscSettingsPanel oscPanel;                             ///< The OSC settings and traffic.

    std::vector<std::unique_ptr<juce::Label>> pluginCaptions;  ///< Captions of the plugin's own rows.
    std::vector<Row> pluginRows;                           ///< The plugin's own rows.
    int preferredHeight = 0;                               ///< See getPreferredHeight().
    bool syncing = false;                                  ///< True while the controls are updated from the settings.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsContent)
};

/** The settings window: a window of its own (native title bar, floats above ordinary windows so that it is in
    front of the host's plug-in window) that shows a SettingsContent. Closing it only hides it. It has a fixed size. */
class SettingsWindow final : public juce::DocumentWindow
{
public:
    /** Creates the window (hidden) around `content`, which the caller owns and keeps alive longer than the window. */
    SettingsWindow (const juce::String& title, SettingsContent& content);

    /** Hides the window. */
    void closeButtonPressed() override { setVisible (false); }

    /** Shows the window in front of the plug-in window; a window that is not showing yet is placed beside it. */
    void show (juce::Component* around);

private:
    SettingsContent& settings;   ///< The content (not owned).

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};
}
