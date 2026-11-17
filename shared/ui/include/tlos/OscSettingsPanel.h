// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file OscSettingsPanel.h
    @brief The OSC section of the settings window: the settings of a plugin instance and a live view of the traffic. */

#pragma once

#include "tlos/SpatialProcessor.h"

namespace tlos
{
/** A small round indicator that lights up for a moment. */
class ActivityLamp final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    /** Creates a lamp that lights up in `colour`. */
    explicit ActivityLamp (juce::Colour colour) : lampColour (colour) {}

    /** Lights the lamp (or puts it out). */
    void setLit (bool shouldBeLit)
    {
        if (lit != shouldBeLit)
        {
            lit = shouldBeLit;
            repaint();
        }
    }

    /** @return Whether the lamp is lit. */
    bool isLit() const noexcept { return lit; }

    /** Draws the lamp: a filled circle when lit, a dim ring otherwise. */
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (12.0f, 12.0f);
        g.setColour (lit ? lampColour : lampColour.withAlpha (0.18f));
        g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawEllipse (r, 1.0f);
    }

private:
    juce::Colour lampColour;   ///< The colour when lit.
    bool lit = false;          ///< Whether the lamp is lit.
};

/** Edits the OSC settings of a processor and shows what OSC is doing.

    Settings: whether to receive and on which port, whether to send and to which host and port, and which
    addresses to send with. Every change is applied at once (and saved with the session); a text field is applied
    when Return is pressed or the field is left. Feedback: a lamp for each of "In", "Out" and "Ignored" that lights
    up for a moment whenever a message was received, sent or not understood, with a running count and the last
    message as text, and a line that says whether the port is open. Message thread only. */
class OscSettingsPanel final : public juce::Component,
                               private juce::Timer
{
public:
    static constexpr int kWidth = 456;    ///< Width of the panel in pixels.
    static constexpr int kHeight = 238;   ///< Height of the panel in pixels.

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
    ActivityLamp& getInLamp() noexcept { return inLamp; }                       ///< Lights for incoming messages.
    ActivityLamp& getOutLamp() noexcept { return outLamp; }                     ///< Lights for outgoing messages.
    ActivityLamp& getIgnoredLamp() noexcept { return ignoredLamp; }             ///< Lights for messages that were not understood.
    juce::Label& getInText() noexcept { return inText; }                        ///< Count and last incoming message.
    juce::Label& getOutText() noexcept { return outText; }                      ///< Count and last outgoing message.
    juce::Label& getIgnoredText() noexcept { return ignoredText; }              ///< Count and last message that was not understood.
    /// @}

    /** Reads the controls and applies them as the processor's OSC settings. */
    void apply();

    /** Updates the status line, the lamps and the texts from the processor (the timer does this at 20 Hz). */
    void refresh();

    /** Lays out the controls. */
    void resized() override;

    /** Fills the background. */
    void paint (juce::Graphics&) override;

private:
    /** Shows the processor's settings in the controls, without applying them again. */
    void show (const OscSettings& settings);
    /** Timer callback. */
    void timerCallback() override { refresh(); }

    SpatialProcessor& processor;                          ///< The processor whose settings are edited.
    juce::ToggleButton receiveToggle { "Receive on port" };  ///< Switches listening on or off.
    juce::TextEditor receivePort;                         ///< The port to listen on.
    juce::ToggleButton sendToggle { "Send changes to" };  ///< Switches sending on or off.
    juce::TextEditor sendHost;                            ///< The host to send to.
    juce::TextEditor sendPort;                            ///< The port to send to.
    juce::Label formatLabel { {}, "Send as" };            ///< Caption of the format menu.
    juce::ComboBox formatBox;                             ///< Own addresses, ADM-OSC or both.
    juce::Label status;                                   ///< What OSC is doing now.
    ActivityLamp inLamp { juce::Colour (0xff6fd16f) };        ///< Lit while messages arrive.
    ActivityLamp outLamp { juce::Colour (0xff5aa9ff) };       ///< Lit while messages are sent.
    ActivityLamp ignoredLamp { juce::Colour (0xffff7a7a) };   ///< Lit when a message was not understood.
    juce::Label inLabel { {}, "In" };                     ///< Caption of the incoming line.
    juce::Label outLabel { {}, "Out" };                   ///< Caption of the outgoing line.
    juce::Label ignoredLabel { {}, "Ignored" };           ///< Caption of the ignored line.
    juce::Label inText;                                   ///< Count and last incoming message.
    juce::Label outText;                                  ///< Count and last outgoing message.
    juce::Label ignoredText;                              ///< Count and last message that was not understood.
    int lastReceived = 0;                                 ///< Received count at the last refresh().
    int lastSent = 0;                                     ///< Sent count at the last refresh().
    int lastIgnored = 0;                                  ///< Ignored count at the last refresh().
    juce::uint32 inLitAt = 0;                             ///< Millisecond counter when the in lamp was last lit.
    juce::uint32 outLitAt = 0;                            ///< Millisecond counter when the out lamp was last lit.
    juce::uint32 ignoredLitAt = 0;                        ///< Millisecond counter when the ignored lamp was last lit.
    bool showing = false;                                 ///< True while the controls are filled in (ignores their callbacks).

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscSettingsPanel)
};
}
