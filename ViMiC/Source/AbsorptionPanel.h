// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file AbsorptionPanel.h
    @brief The panel in the settings window for the absorption of the six walls in three bands, with surface presets. */

#pragma once

#include "PluginProcessor.h"

#include <array>
#include <memory>

/** The absorption of the walls: a row for each wall (left, right, front, rear, floor, ceiling) with a menu of surface presets and the absorption in
    percent in the low, the middle and the high band, and a row on top that gives all the walls the same preset. The sliders are attached to the host parameters;
    a preset writes the three parameters of a wall (inside a change gesture each, so the host records it). The menus follow the parameters: a wall that
    matches a preset shows its name, any other "custom". Message thread only. */
class AbsorptionPanel final : public juce::Component,
                              private juce::Timer
{
public:
    static constexpr int kRowHeight = 26;                              ///< Height of a row in pixels.
    static constexpr int kHeight = (vimic::WallCount + 2) * kRowHeight;   ///< Height of the whole panel: a header, the row for all walls and one per wall.

    /** Creates the panel for `processor` and starts a 10 Hz timer that keeps the menus in step with the parameters. */
    explicit AbsorptionPanel (ViMiCAudioProcessor& processor);

    /** Stops the timer. */
    ~AbsorptionPanel() override;

    /** @return The preset menu of a wall (vimic::Wall). */
    juce::ComboBox& getPresetBox (int wall) noexcept { return *wallBoxes[(size_t) wall]; }
    /** @return The menu that gives every wall the same preset. */
    juce::ComboBox& getAllBox() noexcept { return allBox; }
    /** @return The slider of the absorption of a wall in a band (0 low, 1 mid, 2 high). */
    juce::Slider& getSlider (int wall, int band) noexcept { return *sliders[(size_t) (3 * wall + band)]; }

    /** Writes a preset to the three parameters of a wall (inside change gestures). @param wall  The wall (vimic::Wall). @param preset  The preset. */
    void applyPreset (int wall, const vimic::SurfacePreset& preset);

    /** Brings the menus up to date with the parameters (the timer does this). */
    void refresh();

    /** Lays out the rows. */
    void resized() override;

private:
    /** Timer callback. */
    void timerCallback() override { refresh(); }

    ViMiCAudioProcessor& processor;                                          ///< The processor whose parameters are shown and edited.
    juce::Label lowTitle;                                                    ///< The title of the low band column.
    juce::Label midTitle;                                                    ///< The title of the middle band column.
    juce::Label highTitle;                                                   ///< The title of the high band column.
    juce::Label allLabel { {}, "All walls" };                                ///< Caption of the row for all walls.
    juce::ComboBox allBox;                                                   ///< Gives every wall the same preset.
    std::array<std::unique_ptr<juce::Label>, vimic::WallCount> wallLabels;   ///< The wall names.
    std::array<std::unique_ptr<juce::ComboBox>, vimic::WallCount> wallBoxes; ///< The preset menu of every wall.
    std::array<std::unique_ptr<juce::Slider>, 3 * vimic::WallCount> sliders; ///< The absorption sliders of every wall.
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, 3 * vimic::WallCount> attachments;   ///< Tie the sliders to their parameters.
    bool syncing = false;                                                    ///< True while the menus are updated from the parameters.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AbsorptionPanel)
};
