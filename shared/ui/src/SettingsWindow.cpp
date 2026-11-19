// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file SettingsWindow.cpp
    @brief The settings window and its content. */

#include "tlos/SettingsWindow.h"
#include "tlos/Parameters.h"
#include "tlos/PositionTableWindow.h"

namespace tlos
{
void placeBeside (juce::TopLevelWindow& window, juce::Component* around)
{
    if (around == nullptr)
        return;

    const auto mainBounds = around->getTopLevelComponent()->getScreenBounds();
    juce::Rectangle<int> available = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                                         ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt()
                                         : juce::Rectangle<int>();
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (mainBounds))
        available = display->userBounds.toNearestInt();

    juce::BorderSize<int> frame;
    if (auto* peer = window.getPeer())
        frame = peer->getFrameSize();

    if (const auto position = PositionTableWindow::chooseBesidePosition (mainBounds, available, window.getLocalBounds(), frame, 8))
        window.setTopLeftPosition (*position);
    else
        window.centreAroundComponent (around, window.getWidth(), window.getHeight());
}

//==============================================================================
SettingsContent::SettingsContent (SpatialProcessor& p) : processor (p), oscPanel (p)
{
    for (auto* header : { &roomHeader, &panningHeader, &oscHeader })
    {
        header->setFont (juce::FontOptions (14.0f, juce::Font::bold));
        header->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
        addAndMakeVisible (header);
    }
    for (auto* label : { &roomLabel, &coordinateLabel, &outsideLabel, &virtualLabel })
    {
        label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (label);
    }

    // The room: how far the pad reaches and where dragged sources and speakers stop (same for every plugin).
    roomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    roomSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    roomSlider.setRange (params::kMinRoomHalf, params::kMaxRoomHalf, 0.5);
    roomSlider.setSkewFactorFromMidPoint (20.0);
    roomSlider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " m"; };
    roomSlider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
    roomSlider.setTooltip ("Half the width of the room in metres. The pad shows this much on each side of you, and sources and "
                           "speakers stop at its edge when dragged. Positions beyond it are still possible (tables, files, host "
                           "automation, up to 100 m) and sound as usual.");
    roomSlider.setValue (processor.getRoomHalfExtent(), juce::dontSendNotification);
    roomSlider.onValueChange = [this]
    {
        if (! syncing)
            processor.setRoomHalfExtent (roomSlider.getValue());
    };
    addAndMakeVisible (roomSlider);

    // How the tables show positions; the host parameters stay x and y either way.
    coordinateBox.addItem ("Cartesian (x, y)", 1);
    coordinateBox.addItem ("Polar (azimuth, distance)", 2);
    coordinateBox.setTooltip ("How the tables show and edit positions. Azimuth is in degrees, counter-clockwise from the front. "
                              "The host parameters, files and the pad always use x and y in metres.");
    coordinateBox.setSelectedId (processor.getCoordinateMode() == SpatialProcessor::CoordinateMode::Polar ? 2 : 1, juce::dontSendNotification);
    coordinateBox.onChange = [this]
    {
        if (! syncing)
            processor.setCoordinateMode (coordinateBox.getSelectedId() == 2 ? SpatialProcessor::CoordinateMode::Polar
                                                                            : SpatialProcessor::CoordinateMode::Cartesian);
    };
    addAndMakeVisible (coordinateBox);

    // How the level falls for a source outside the speaker layout (same for every plugin).
    outsideSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    outsideSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    outsideSlider.setTooltip ("Level drop in dB for each doubling of (1 + the distance in metres outside the edge of the speaker layout). "
                              "6 dB is the free-field value. 0 = the level stays constant outside the layout.");
    addAndMakeVisible (outsideSlider);
    outsideLabel.setVisible (processor.usesOutsideRolloff());
    outsideSlider.setVisible (processor.usesOutsideRolloff());
    if (auto* parameter = processor.getValueTreeState().getParameter (params::kOutsideRolloffId))
    {
        outsideAttachment = std::make_unique<juce::SliderParameterAttachment> (*parameter, outsideSlider, nullptr);
        outsideSlider.textFromValueFunction = [] (double v) { return juce::String (v, 2) + " dB"; };
        outsideSlider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
        outsideSlider.updateText();
    }

    // Virtual (imaginary) speakers: Auto finds them from the speaker layout, Manual lets the user place them
    // (double-click the pad to add or delete, drag to move; speakers must be set to Edit), Off uses the real speakers only.
    virtualModeBox.addItem ("Auto", (int) VirtualMode::Auto);
    virtualModeBox.addItem ("Manual", (int) VirtualMode::Manual);
    virtualModeBox.addItem ("Off", (int) VirtualMode::Off);
    virtualModeBox.setSelectedId ((int) processor.getVirtualMode(), juce::dontSendNotification);
    virtualModeBox.setTooltip ("Auto: imaginary speakers are added where the layout needs them (open gaps, ring centre). "
                               "Manual: you place them (double-click the pad to add or delete, drag to move; speakers must be set to Edit). "
                               "Off: real speakers only.");
    virtualModeBox.onChange = [this]
    {
        if (! syncing)
            processor.setVirtualMode ((VirtualMode) virtualModeBox.getSelectedId());
    };
    addAndMakeVisible (virtualModeBox);
    virtualLabel.setVisible (processor.supportsVirtualSpeakers());
    virtualModeBox.setVisible (processor.supportsVirtualSpeakers());

    addAndMakeVisible (oscPanel);

    updateLayoutData();
    startTimerHz (10);
}

SettingsContent::~SettingsContent()
{
    stopTimer();
}

void SettingsContent::addRow (const juce::String& caption, juce::Component& control, int height)
{
    juce::Label* labelPointer = nullptr;
    if (caption.isNotEmpty())
    {
        auto label = std::make_unique<juce::Label> ("", caption);
        label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (*label);
        labelPointer = label.get();
        pluginCaptions.push_back (std::move (label));
    }

    addAndMakeVisible (control);
    pluginRows.push_back ({ labelPointer, &control, height > 0 ? height : kRowHeight });
    updateLayoutData();
}

void SettingsContent::addSection (const juce::String& title)
{
    auto label = std::make_unique<juce::Label> ("", title);
    label->setFont (juce::FontOptions (13.0f, juce::Font::bold));
    label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
    addAndMakeVisible (*label);
    pluginRows.push_back ({ label.get(), nullptr, kRowHeight });
    pluginCaptions.push_back (std::move (label));
    updateLayoutData();
}

void SettingsContent::updateLayoutData()
{
    int panningRows = (processor.usesOutsideRolloff() ? kRowHeight : 0) + (processor.supportsVirtualSpeakers() ? kRowHeight : 0);
    for (const auto& pluginRow : pluginRows)
        panningRows += pluginRow.height;

    const int height = 12 + (kRowHeight + 4 + 2 * kRowHeight)                      // Room: header and two rows
                     + 10 + (kRowHeight + 4 + panningRows)                         // Panning: header and rows
                     + 10 + (kRowHeight + 4 + OscSettingsPanel::kHeight) + 12;    // OSC: header and the panel

    if (height != preferredHeight)
    {
        preferredHeight = height;
        if (onSizeChanged)
            onSizeChanged();
    }

    setSize (kWidth, preferredHeight);
}

void SettingsContent::refresh()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    if (! juce::approximatelyEqual (roomSlider.getValue(), processor.getRoomHalfExtent()) && ! roomSlider.isMouseButtonDown())
        roomSlider.setValue (processor.getRoomHalfExtent(), juce::dontSendNotification);

    const int wantedCoordinates = processor.getCoordinateMode() == SpatialProcessor::CoordinateMode::Polar ? 2 : 1;
    if (coordinateBox.getSelectedId() != wantedCoordinates)
        coordinateBox.setSelectedId (wantedCoordinates, juce::dontSendNotification);

    if (virtualModeBox.getSelectedId() != (int) processor.getVirtualMode())
        virtualModeBox.setSelectedId ((int) processor.getVirtualMode(), juce::dontSendNotification);
}

void SettingsContent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff23272e));
}

void SettingsContent::resized()
{
    auto area = getLocalBounds().reduced (12, 12);
    const auto section = [&] (juce::Label& header)
    {
        header.setBounds (area.removeFromTop (kRowHeight));
        area.removeFromTop (4);
    };
    const auto row = [&] (juce::Label& label, juce::Component& control)
    {
        auto r = area.removeFromTop (kRowHeight);
        label.setBounds (r.removeFromLeft (kLabelWidth));
        control.setBounds (r.reduced (0, 2));
    };

    section (roomHeader);
    row (roomLabel, roomSlider);
    {
        auto r = area.removeFromTop (kRowHeight);
        coordinateLabel.setBounds (r.removeFromLeft (kLabelWidth));
        coordinateBox.setBounds (r.removeFromLeft (200).reduced (0, 2));
    }
    area.removeFromTop (10);

    section (panningHeader);
    if (processor.usesOutsideRolloff())
        row (outsideLabel, outsideSlider);
    if (processor.supportsVirtualSpeakers())
    {
        auto r = area.removeFromTop (kRowHeight);
        virtualLabel.setBounds (r.removeFromLeft (kLabelWidth));
        virtualModeBox.setBounds (r.removeFromLeft (120).reduced (0, 2));
    }
    for (auto& pluginRow : pluginRows)
    {
        auto r = area.removeFromTop (pluginRow.height);
        if (pluginRow.control == nullptr)
            pluginRow.caption->setBounds (r);                                    // a heading
        else if (pluginRow.caption == nullptr)
            pluginRow.control->setBounds (r);                                    // a control that takes the whole row
        else
        {
            pluginRow.caption->setBounds (r.removeFromLeft (kLabelWidth));
            pluginRow.control->setBounds (r.reduced (0, 2));
        }
    }
    area.removeFromTop (10);

    section (oscHeader);
    oscPanel.setBounds (area.removeFromTop (OscSettingsPanel::kHeight).withWidth (OscSettingsPanel::kWidth));
}

//==============================================================================
SettingsWindow::SettingsWindow (const juce::String& title, SettingsContent& content)
    : juce::DocumentWindow (title, juce::Colour (0xff23272e), juce::DocumentWindow::closeButton), settings (content)
{
    setUsingNativeTitleBar (true);

    // Plug-in windows are usually floating windows of the host; an ordinary window would open behind them.
    setAlwaysOnTop (true);

    // The content goes in a viewport: with many rows (ViMiC has more than fit on a laptop screen) it scrolls instead of being cut off.
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    setContentNonOwned (&viewport, true);
    setResizable (false, false);

    content.onSizeChanged = [this] { fitToContent(); };
}

void SettingsWindow::fitToContent()
{
    // Leave room for the title bar and the menu bar and dock around the window.
    int maxHeight = 100000;
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        maxHeight = std::max (300, juce::roundToInt (display->userBounds.getHeight()) - 110);

    const int wanted = settings.getPreferredHeight();
    scrolling = wanted > maxHeight;
    setContentComponentSize (SettingsContent::kWidth + (scrolling ? viewport.getScrollBarThickness() : 0), std::min (wanted, maxHeight));
}

void SettingsWindow::show (juce::Component* around)
{
    if (! isVisible())
    {
        fitToContent();
        placeBeside (*this, around);
    }

    setAlwaysOnTop (true);
    setVisible (true);
    settings.refresh();
    toFront (true);
}
}
