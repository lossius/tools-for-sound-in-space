// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file AbsorptionPanel.cpp
    @brief The wall absorption panel of the settings window. */

#include "AbsorptionPanel.h"

#include <cmath>

namespace
{
constexpr int kLabelWidth = 62;     ///< Width of the wall names in pixels.
constexpr int kBoxWidth = 128;      ///< Width of the preset menus in pixels.
constexpr int kSliderWidth = 78;    ///< Width of an absorption slider in pixels.
constexpr int kGap = 6;             ///< Space between the columns in pixels.

/** Menu item IDs: 1 is "custom" (no preset), the presets follow. */
constexpr int kCustomId = 1, kFirstPresetId = 2;

/** Fills a preset menu. @param box  The menu. @param first  The text of the first item (shown when nothing matches). */
void fillPresetBox (juce::ComboBox& box, const juce::String& first)
{
    box.addItem (first, kCustomId);
    box.addSeparator();
    int id = kFirstPresetId;
    for (const auto& preset : vimic::surfacePresets())
        box.addItem (juce::String (preset.name.data(), preset.name.size()), id++);
}
}

AbsorptionPanel::AbsorptionPanel (ViMiCAudioProcessor& p) : processor (p)
{
    for (auto* title : { &lowTitle, &midTitle, &highTitle })
    {
        title->setFont (juce::FontOptions (12.0f));
        title->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        title->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (title);
    }
    lowTitle.setText ("Low %", juce::dontSendNotification);
    midTitle.setText ("Mid %", juce::dontSendNotification);
    highTitle.setText ("High %", juce::dontSendNotification);

    allLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    addAndMakeVisible (allLabel);
    fillPresetBox (allBox, "Apply to all...");
    allBox.setTooltip ("Gives every wall the same surface.");
    allBox.onChange = [this]
    {
        if (syncing || allBox.getSelectedId() < kFirstPresetId)
            return;

        const auto& preset = vimic::surfacePresets()[(size_t) (allBox.getSelectedId() - kFirstPresetId)];
        for (int wall = 0; wall < vimic::WallCount; ++wall)
            applyPreset (wall, preset);

        const juce::ScopedValueSetter<bool> guard (syncing, true);
        allBox.setSelectedId (kCustomId, juce::dontSendNotification);
        refresh();
    };
    addAndMakeVisible (allBox);
    allBox.setSelectedId (kCustomId, juce::dontSendNotification);

    auto& apvts = p.getValueTreeState();
    for (int wall = 0; wall < vimic::WallCount; ++wall)
    {
        wallLabels[(size_t) wall] = std::make_unique<juce::Label> ("", vimic::params::wallName (wall));
        wallLabels[(size_t) wall]->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (*wallLabels[(size_t) wall]);

        auto box = std::make_unique<juce::ComboBox>();
        fillPresetBox (*box, "custom");
        box->setTooltip ("The surface of this wall. Changing the three values by hand makes it custom.");
        box->onChange = [this, wall]
        {
            const auto& boxRef = *wallBoxes[(size_t) wall];
            if (syncing || boxRef.getSelectedId() < kFirstPresetId)
                return;

            applyPreset (wall, vimic::surfacePresets()[(size_t) (boxRef.getSelectedId() - kFirstPresetId)]);
        };
        addAndMakeVisible (*box);
        wallBoxes[(size_t) wall] = std::move (box);

        for (int band = 0; band < 3; ++band)
        {
            auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::NoTextBox);
            slider->setTooltip ("How much of the energy this wall absorbs in this band, in percent (0.1 reflects nearly everything, 99.9 nearly nothing).");
            slider->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            const auto id = vimic::params::absorptionId (wall, band);
            auto* parameter = apvts.getParameter (id);
            jassert (parameter != nullptr);
            attachments[(size_t) (3 * wall + band)] = std::make_unique<juce::SliderParameterAttachment> (*parameter, *slider, nullptr);
            slider->textFromValueFunction = [] (double v) { return juce::String (v, 1); };
            slider->setColour (juce::Slider::trackColourId, juce::Colour (0xff3a5f80));
            addAndMakeVisible (*slider);
            slider->updateText();
            sliders[(size_t) (3 * wall + band)] = std::move (slider);
        }
    }

    refresh();
    startTimerHz (10);
}

AbsorptionPanel::~AbsorptionPanel()
{
    stopTimer();
}

void AbsorptionPanel::applyPreset (int wall, const vimic::SurfacePreset& preset)
{
    const double values[3] { preset.surface.low, preset.surface.mid, preset.surface.high };
    for (int band = 0; band < 3; ++band)
        if (auto* parameter = processor.getValueTreeState().getParameter (vimic::params::absorptionId (wall, band)))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) values[band]));
            parameter->endChangeGesture();
        }
}

void AbsorptionPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);
    const auto walls = processor.getWalls();

    for (int wall = 0; wall < vimic::WallCount; ++wall)
    {
        const auto& s = walls.surface[(size_t) wall];
        int shown = kCustomId, id = kFirstPresetId;
        for (const auto& preset : vimic::surfacePresets())
        {
            if (std::abs (preset.surface.low - s.low) < 0.06 && std::abs (preset.surface.mid - s.mid) < 0.06 && std::abs (preset.surface.high - s.high) < 0.06)
            {
                shown = id;
                break;
            }
            ++id;
        }

        auto& box = *wallBoxes[(size_t) wall];
        if (box.getSelectedId() != shown)
            box.setSelectedId (shown, juce::dontSendNotification);
    }
}

void AbsorptionPanel::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (kRowHeight);
    header.removeFromLeft (kLabelWidth + kBoxWidth + 2 * kGap);
    for (auto* title : { &lowTitle, &midTitle, &highTitle })
    {
        title->setBounds (header.removeFromLeft (kSliderWidth));
        header.removeFromLeft (kGap);
    }

    auto top = area.removeFromTop (kRowHeight);
    allLabel.setBounds (top.removeFromLeft (kLabelWidth));
    top.removeFromLeft (kGap);
    allBox.setBounds (top.removeFromLeft (kBoxWidth).reduced (0, 2));

    for (int wall = 0; wall < vimic::WallCount; ++wall)
    {
        auto row = area.removeFromTop (kRowHeight);
        wallLabels[(size_t) wall]->setBounds (row.removeFromLeft (kLabelWidth));
        row.removeFromLeft (kGap);
        wallBoxes[(size_t) wall]->setBounds (row.removeFromLeft (kBoxWidth).reduced (0, 2));
        row.removeFromLeft (kGap);
        for (int band = 0; band < 3; ++band)
        {
            sliders[(size_t) (3 * wall + band)]->setBounds (row.removeFromLeft (kSliderWidth).reduced (0, 3));
            row.removeFromLeft (kGap);
        }
    }
}
