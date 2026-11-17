// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file IconButton.h
    @brief A button that shows a drawn icon instead of text, so that it looks the same on every platform. */

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tlos
{
/** A push button with an icon that is drawn as vector graphics (not a font glyph), so that it looks the same on
    macOS and Windows and needs no font. The background is the look and feel's button, so it matches the text buttons.
    The icon uses the text colour of the button and is dimmed while the button is disabled. */
class IconButton final : public juce::TextButton
{
public:
    /** Which icon to draw. */
    enum class Icon
    {
        ZoomIn,    ///< A magnifying glass with a plus.
        ZoomOut    ///< A magnifying glass with a minus.
    };

    /** Creates the button.
        @param icon   The icon to draw.
        @param title  The name of the button for screen readers (it has no text). */
    IconButton (Icon icon, const juce::String& title) : iconToDraw (icon)
    {
        setTitle (title);
        setName (title);
    }

    /** @return The icon of the button. */
    Icon getIcon() const noexcept { return iconToDraw; }

    /** Draws the button's background and then the icon. */
    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (getToggleState() ? buttonOnColourId : buttonColourId),
                                               isMouseOver, isButtonDown);

        const auto colour = findColour (getToggleState() ? textColourOnId : textColourOffId).withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f);
        const auto area = getLocalBounds().toFloat().reduced (juce::jmin (6.0f, (float) getWidth() * 0.2f), 4.0f);
        const float size = juce::jmin (area.getWidth(), area.getHeight());
        const auto box = juce::Rectangle<float> (size, size).withCentre (area.getCentre());

        // The lens: a circle in the upper left of the box; the handle: a short thick line to the lower right.
        const float radius = size * 0.34f;
        const auto centre = juce::Point<float> (box.getX() + radius + 0.5f, box.getY() + radius + 0.5f);
        g.setColour (colour);
        g.drawEllipse (juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (centre), 1.5f);
        const auto handleStart = centre + juce::Point<float> (radius, radius) * 0.72f;
        g.drawLine (juce::Line<float> (handleStart, box.getBottomRight()), 2.2f);

        // The sign in the lens.
        const float arm = radius * 0.55f;
        g.drawLine (centre.x - arm, centre.y, centre.x + arm, centre.y, 1.4f);
        if (iconToDraw == Icon::ZoomIn)
            g.drawLine (centre.x, centre.y - arm, centre.x, centre.y + arm, 1.4f);
    }

private:
    Icon iconToDraw;   ///< The icon.
};
}
