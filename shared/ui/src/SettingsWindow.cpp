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
    roomHeader.setText (processor.getDisplaySectionTitle(), juce::dontSendNotification);
    roomLabel.setText (processor.getPadSizeCaption(), juce::dontSendNotification);
    roomSlider.setTooltip (processor.getPadSizeTooltip());
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

bool SettingsContent::hasTabs() const noexcept
{
    for (const auto& row : pluginRows)
        if (row.control == nullptr)
            return true;

    return false;
}

void SettingsContent::buildTabs()
{
    const bool tabbed = hasTabs();

    // The section headings of the single column are not used with tabs, and the plugin's own headings only name the tabs.
    for (auto* header : { &roomHeader, &panningHeader, &oscHeader })
        header->setVisible (! tabbed);
    for (auto& row : pluginRows)
        if (row.control == nullptr)
            row.caption->setVisible (false);

    if (! tabbed)
    {
        tabs.clear();
        tabButtons.clear();
        activeTab = 0;
        return;
    }

    const auto oldTitles = [this]
    {
        juce::StringArray titles;
        for (const auto& tab : tabs)
            titles.add (tab.title);
        return titles;
    }();

    tabs.clear();

    // The first tab: the pad size and the coordinates, the outside rolloff and the virtual speakers (if there are any).
    Tab display;
    display.title = processor.getDisplaySectionTitle();
    display.items.push_back ({ &roomLabel, &roomSlider, kRowHeight, 0 });
    display.items.push_back ({ &coordinateLabel, &coordinateBox, kRowHeight, 200 });
    if (processor.usesOutsideRolloff())
        display.items.push_back ({ &outsideLabel, &outsideSlider, kRowHeight, 0 });
    if (processor.supportsVirtualSpeakers())
        display.items.push_back ({ &virtualLabel, &virtualModeBox, kRowHeight, 120 });
    tabs.push_back (std::move (display));

    // Rows before the first section go to the first tab; every section of the plugin is a tab.
    for (const auto& row : pluginRows)
    {
        if (row.control == nullptr)
        {
            Tab tab;
            tab.title = row.caption->getText();
            tabs.push_back (std::move (tab));
        }
        else
        {
            tabs.back().items.push_back ({ row.caption, row.control, row.height, 0 });
        }
    }

    Tab osc;
    osc.title = "OSC";
    osc.items.push_back ({ nullptr, &oscPanel, OscSettingsPanel::kHeight, OscSettingsPanel::kWidth });
    tabs.push_back (std::move (osc));

    for (auto& tab : tabs)
    {
        tab.height = 0;
        for (const auto& item : tab.items)
            tab.height += item.height;
    }

    activeTab = std::clamp (activeTab, 0, (int) tabs.size() - 1);

    juce::StringArray titles;
    for (const auto& tab : tabs)
        titles.add (tab.title);

    if (titles != oldTitles || tabButtons.size() != tabs.size())
    {
        tabButtons.clear();
        for (int i = 0; i < (int) tabs.size(); ++i)
        {
            auto button = std::make_unique<juce::TextButton> (tabs[(size_t) i].title);
            button->setClickingTogglesState (true);
            button->setRadioGroupId (4711);
            button->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a5f80));
            button->onClick = [this, i] { setActiveTab (i); };
            addAndMakeVisible (*button);
            tabButtons.push_back (std::move (button));
        }
    }

    showActiveTab();
}

void SettingsContent::showActiveTab()
{
    for (int i = 0; i < (int) tabs.size(); ++i)
        for (const auto& item : tabs[(size_t) i].items)
        {
            if (item.caption != nullptr)
                item.caption->setVisible (i == activeTab);
            item.control->setVisible (i == activeTab);
        }

    for (int i = 0; i < (int) tabButtons.size(); ++i)
        tabButtons[(size_t) i]->setToggleState (i == activeTab, juce::dontSendNotification);
}

void SettingsContent::setActiveTab (int index)
{
    if (tabs.empty())
        return;

    activeTab = std::clamp (index, 0, (int) tabs.size() - 1);
    showActiveTab();
    resized();
}

int SettingsContent::tabOf (const juce::Component* component) const noexcept
{
    for (int i = 0; i < (int) tabs.size(); ++i)
        for (const auto& item : tabs[(size_t) i].items)
            if (item.control == component || item.caption == component)
                return i;

    return -1;
}

void SettingsContent::updateLayoutData()
{
    buildTabs();

    if (! tabs.empty())
    {
        int tallest = 0;
        for (const auto& tab : tabs)
            tallest = std::max (tallest, tab.height);

        const int height = 12 + kTabBarHeight + 10 + tallest + 12;
        if (height != preferredHeight)
        {
            preferredHeight = height;
            if (onSizeChanged)
                onSizeChanged();
        }

        setSize (kWidth, preferredHeight);
        resized();
        return;
    }

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
    if (! tabs.empty())
    {
        auto page = getLocalBounds().reduced (12, 12);

        auto bar = page.removeFromTop (kTabBarHeight);
        const int buttonWidth = bar.getWidth() / std::max ((int) tabButtons.size(), 1);
        for (auto& button : tabButtons)
            button->setBounds (bar.removeFromLeft (buttonWidth).reduced (1, 0));

        page.removeFromTop (10);
        for (const auto& item : tabs[(size_t) activeTab].items)
        {
            auto r = page.removeFromTop (item.height);
            if (item.caption == nullptr)
            {
                item.control->setBounds (item.controlWidth > 0 ? r.withWidth (item.controlWidth) : r);   // a control that takes the whole row
            }
            else
            {
                item.caption->setBounds (r.removeFromLeft (kLabelWidth));
                item.control->setBounds ((item.controlWidth > 0 ? r.removeFromLeft (item.controlWidth) : r).reduced (0, 2));
            }
        }

        return;
    }

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
