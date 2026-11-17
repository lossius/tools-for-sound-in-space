// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file OscSettingsPanel.h
    @brief The small panel where the OSC settings of a plugin instance are edited. */

#pragma once

#include "tlos/SpatialProcessor.h"

namespace tlos
{
/** Edits the OSC settings of a processor: whether to receive and on which port, whether to send and to where, and
    which addresses to send with. Every change is applied at once (and saved with the session); a text field is
    applied when Return is pressed or the field is left. A line at the bottom says what OSC is doing now. Message
    thread only. */
class OscSettingsPanel final : public juce::Component,
                               private juce::Timer
{
public:
    /** Creates the panel for `processor` and shows its current settings. */
    explicit OscSettingsPanel (SpatialProcessor& processor);

    /** Stops the timer. */
    ~OscSettingsPanel() override;

    /// @name Access to the controls (used by the tests)
    /// @{
    juce::ToggleButton& getReceiveToggle() noexcept { return receiveToggle; }   ///< "Receive".
    juce::TextEditor& getReceivePortEditor() noexcept { return receivePort; }   ///< Port to listen on.
    juce::ToggleButton& getSendToggle() noexcept { return sendToggle; }         ///< "Send".
    juce::TextEditor& getSendHostEditor() noexcept { return sendHost; }         ///< Host to send to.
    juce::TextEditor& getSendPortEditor() noexcept { return sendPort; }         ///< Port to send to.
    juce::ComboBox& getFormatBox() noexcept { return formatBox; }               ///< Addresses to send with.
    juce::Label& getStatusLabel() noexcept { return status; }                   ///< What OSC is doing.
    /// @}

    /** Reads the controls and applies them as the processor's OSC settings. */
    void apply();

    /** Lays out the controls. */
    void resized() override;

    /** Fills the background. */
    void paint (juce::Graphics&) override;

private:
    /** Shows the processor's settings in the controls, without applying them again. */
    void show (const OscSettings& settings);
    /** Updates the status line. */
    void timerCallback() override { status.setText (processor.getOscStatus(), juce::dontSendNotification); }

    SpatialProcessor& processor;                          ///< The processor whose settings are edited.
    juce::ToggleButton receiveToggle { "Receive on port" };  ///< Switches listening on or off.
    juce::TextEditor receivePort;                         ///< The port to listen on.
    juce::ToggleButton sendToggle { "Send changes to" };  ///< Switches sending on or off.
    juce::TextEditor sendHost;                            ///< The host to send to.
    juce::TextEditor sendPort;                            ///< The port to send to.
    juce::Label formatLabel { {}, "Send as" };            ///< Caption of the format menu.
    juce::ComboBox formatBox;                             ///< Own addresses, ADM-OSC or both.
    juce::Label status;                                   ///< What OSC is doing now.
    bool showing = false;                                 ///< True while the controls are filled in (ignores their callbacks).

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscSettingsPanel)
};
}
