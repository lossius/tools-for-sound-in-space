// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file OscSettingsPanel.cpp
    @brief The OSC settings panel. */

#include "tlos/OscSettingsPanel.h"

namespace tlos
{
OscSettingsPanel::OscSettingsPanel (SpatialProcessor& p) : processor (p)
{
    const auto setUpPort = [this] (juce::TextEditor& editor)
    {
        editor.setInputRestrictions (5, "0123456789");
        editor.setJustification (juce::Justification::centredRight);
        editor.onReturnKey = [this] { apply(); };
        editor.onFocusLost = [this] { apply(); };
        addAndMakeVisible (editor);
    };
    setUpPort (receivePort);
    setUpPort (sendPort);

    sendHost.setInputRestrictions (253);
    sendHost.onReturnKey = [this] { apply(); };
    sendHost.onFocusLost = [this] { apply(); };
    addAndMakeVisible (sendHost);

    receiveToggle.setTooltip ("Listen for OSC messages (UDP) on this port. Switch it off when another instance or program uses the port.");
    sendToggle.setTooltip ("Send every change of positions, trims, mutes and so on to this host and port, so that controllers follow the plugin.");
    for (auto* toggle : { &receiveToggle, &sendToggle })
    {
        toggle->onClick = [this] { apply(); };
        addAndMakeVisible (toggle);
    }

    formatBox.addItem ("Own addresses", 1);
    formatBox.addItem ("ADM-OSC", 2);
    formatBox.addItem ("Both", 3);
    formatBox.setTooltip ("Which addresses changes are sent with. Incoming messages of both kinds are always understood.");
    formatBox.onChange = [this] { apply(); };
    addAndMakeVisible (formatBox);
    formatLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    addAndMakeVisible (formatLabel);

    status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    status.setFont (juce::FontOptions (12.0f));
    status.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (status);

    // Live feedback: a lamp, a count and the last message for what comes in, what goes out and what is not understood.
    inLamp.setTooltip ("Lights when an OSC message was received and applied.");
    outLamp.setTooltip ("Lights when an OSC message was sent.");
    ignoredLamp.setTooltip ("Lights when a message arrived that was not understood (unknown address or wrong arguments).");
    for (auto* lamp : { &inLamp, &outLamp, &ignoredLamp })
        addAndMakeVisible (lamp);
    for (auto* label : { &inLabel, &outLabel, &ignoredLabel })
    {
        label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (label);
    }
    for (auto* text : { &inText, &outText, &ignoredText })
    {
        text->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        text->setFont (juce::FontOptions (12.0f));
        text->setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (text);
    }

    show (processor.getOscSettings());
    setSize (kWidth, kHeight);
    refresh();
    startTimerHz (20);
}

void OscSettingsPanel::refresh()
{
    const auto& osc = processor.getOscControl();
    const auto now = juce::Time::getMillisecondCounter();
    constexpr juce::uint32 lampTime = 180;

    const auto follow = [&] (int count, int& last, juce::uint32& litAt, ActivityLamp& lamp, juce::Label& text, const juce::String& message)
    {
        if (count != last)
        {
            last = count;
            litAt = now;
        }
        lamp.setLit (litAt != 0 && now - litAt < lampTime);
        text.setText (juce::String (count) + (message.isNotEmpty() ? "   " + message : juce::String()), juce::dontSendNotification);
    };

    follow (osc.getReceivedCount(), lastReceived, inLitAt, inLamp, inText, osc.getLastReceived());
    follow (osc.getSentCount(), lastSent, outLitAt, outLamp, outText, osc.getLastSent());
    follow (osc.getIgnoredCount(), lastIgnored, ignoredLitAt, ignoredLamp, ignoredText, osc.getLastIgnored());

    status.setText (processor.getOscStatus(), juce::dontSendNotification);
}

OscSettingsPanel::~OscSettingsPanel()
{
    stopTimer();
}

void OscSettingsPanel::show (const OscSettings& s)
{
    const juce::ScopedValueSetter<bool> guard (showing, true);
    receiveToggle.setToggleState (s.receive, juce::dontSendNotification);
    receivePort.setText (juce::String (s.receivePort), false);
    sendToggle.setToggleState (s.send, juce::dontSendNotification);
    sendHost.setText (s.sendHost, false);
    sendPort.setText (juce::String (s.sendPort), false);
    formatBox.setSelectedId ((int) s.format + 1, juce::dontSendNotification);
}

void OscSettingsPanel::apply()
{
    if (showing)
        return;

    OscSettings s = processor.getOscSettings();
    s.receive = receiveToggle.getToggleState();
    s.receivePort = juce::jlimit (1, 65535, receivePort.getText().getIntValue() > 0 ? receivePort.getText().getIntValue() : s.receivePort);
    s.send = sendToggle.getToggleState();
    s.sendHost = sendHost.getText().trim().isNotEmpty() ? sendHost.getText().trim() : s.sendHost;
    s.sendPort = juce::jlimit (1, 65535, sendPort.getText().getIntValue() > 0 ? sendPort.getText().getIntValue() : s.sendPort);
    s.format = (OscFormat) juce::jlimit (0, 2, formatBox.getSelectedId() - 1);

    if (! (s == processor.getOscSettings()))
        processor.setOscSettings (s);

    show (processor.getOscSettings());   // what was kept (a rejected port or host), as it is now
    refresh();
}

void OscSettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff23272e));
}

void OscSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 10);
    const auto row = [&] { auto r = area.removeFromTop (28); area.removeFromTop (6); return r; };

    auto r1 = row();
    receiveToggle.setBounds (r1.removeFromLeft (150));
    receivePort.setBounds (r1.removeFromLeft (70));

    auto r2 = row();
    sendToggle.setBounds (r2.removeFromLeft (150));
    sendPort.setBounds (r2.removeFromRight (70));
    r2.removeFromRight (6);
    sendHost.setBounds (r2);

    auto r3 = row();
    formatLabel.setBounds (r3.removeFromLeft (150));
    formatBox.setBounds (r3.removeFromLeft (150));

    const auto feedback = [&] (ActivityLamp& lamp, juce::Label& label, juce::Label& text)
    {
        auto r = area.removeFromTop (24);
        lamp.setBounds (r.removeFromLeft (20));
        label.setBounds (r.removeFromLeft (56));
        text.setBounds (r);
    };
    area.removeFromTop (4);
    feedback (inLamp, inLabel, inText);
    feedback (outLamp, outLabel, outText);
    feedback (ignoredLamp, ignoredLabel, ignoredText);
    area.removeFromTop (4);
    status.setBounds (area.removeFromTop (22));
}
}
