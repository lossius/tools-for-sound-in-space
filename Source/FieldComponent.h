// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file FieldComponent.h
    @brief The XY pad: draws sources, speakers, triangulation and links, and edits positions by mouse. */

#pragma once

#include "PluginProcessor.h"
#include "Parameters.h"
#include "tripan/SpeakerLayout.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

/** Top-down view of the plane: speakers (squares), sources (circles), the Delaunay triangulation,
    and the links from each source to the speakers it feeds. Handles can be dragged. */
class FieldComponent final : public juce::Component,
                             public juce::SettableTooltipClient,
                             private juce::Timer
{
public:
    /** Creates the pad and starts a 30 Hz timer that follows the parameters. */
    explicit FieldComponent (TriPanAudioProcessor& processor);

    /** Stops the timer. */
    ~FieldComponent() override;

    /** A source, a speaker, or (isVirtual, with isSource false) a virtual speaker from the manual list. */
    struct Handle
    {
        bool isSource;           ///< True for a source, false for a speaker.
        int index;               ///< Zero-based index within its kind.
        bool isVirtual = false;  ///< True for a manual virtual speaker (then isSource is false).
    };

    /** Shows or hides the triangles. */
    void setShowTriangulation (bool shouldShow);

    /** Radius in pixels of a source's inner circle. This is also its mouse target: the (semi-transparent)
        spread circle around it is only a display and never takes part in mouse interaction. */
    static constexpr float kSourceRadius = 9.0f;

    /** Spread: hold Option (Alt on Windows) and drag a source vertically. Moving the mouse up
        increases the spread, moving it down decreases it, sideways movement has no effect. The change
        is relative to where the drag starts and is limited to 0..10 m at every step, so dragging
        down past zero and back up increases the spread again at once. Option + double-click resets
        the spread to 0. Only Option is used (not Cmd). */
    static bool isSpreadModifier (const juce::ModifierKeys& mods) noexcept;

    /** @return The spread of a source in metres (0 to 10). */
    double getSpread (int source) const;

    /** Sets the spread of a source in metres (limited to 0..10). Does nothing if sources are set to View. */
    void setSpread (int source, double metres);

    /// @name Mouse actions
    /// The mouse handlers call these, so that they can be tested without real mouse events.
    /// Positions are in component coordinates.
    /// @{
    /** Mouse button pressed: grabs the handle under `p`, if any (with the spread modifier: for spread editing). */
    void pressAt (juce::Point<float> p, const juce::ModifierKeys& mods);
    /** Mouse moved with the button down: moves the grabbed handle or changes its spread. */
    void dragTo (juce::Point<float> p);
    /** Mouse button released: ends the drag and its change gesture. */
    void release();
    /** Double click: with the spread modifier on a source resets its spread; in Manual virtual mode adds or deletes a virtual speaker. */
    void doubleClickAt (juce::Point<float> p, const juce::ModifierKeys& mods);
    /// @}

    /** Zoom. The full view shows the whole +-10 m plane. Zoom in moves a step towards the area that
        contains all speakers in use (and stops there); zoom out moves a step back towards the full
        view. The view is fixed once set, so it does not shift while speakers are dragged. */
    void zoomIn();
    /** @copydoc zoomIn() */
    void zoomOut();
    /** @return Whether zoomIn() would change the view. */
    bool canZoomIn() const;
    /** @return Whether zoomOut() would change the view. */
    bool canZoomOut() const;
    /** @return Half the width of the visible area in metres (10 = the entire plane). */
    double getViewHalfExtent() const noexcept { return viewHalf; }
    /** @return The centre of the visible area in metres. */
    juce::Point<double> getViewCentre() const noexcept { return viewCentre; }

    /** Re-reads the parameters and repaints if anything changed (also called by a 30 Hz timer). */
    void refresh() { timerCallback(); }

    /** Plane coordinates (metres) -> component coordinates. */
    juce::Point<float> toView (double x, double y) const;
    /** Topmost handle within grab distance of p (component coordinates), sources before speakers. */
    std::optional<Handle> findHandleAt (juce::Point<float> p) const;
    /** Sets the handle's parameters from a component-coordinate position (clamped to the range). */
    void moveHandle (const Handle& handle, juce::Point<float> p);

    /** Manual virtual speakers can be added, moved and deleted when the virtual speakers are set to
        Manual and the speakers are set to Edit. Positions are component coordinates. Returns false
        if not possible (wrong mode, locked, list full, or nothing to delete there). */
    bool canEditVirtualSpeakers() const;
    /** Adds a manual virtual speaker at `p`. @return false if not possible. */
    bool addVirtualSpeakerAt (juce::Point<float> p);
    /** Deletes manual virtual speaker `index`. @return false if not possible. */
    bool removeVirtualSpeaker (int index);

    /** Draws grid, triangulation, spread circles, links, speakers and sources. */
    void paint (juce::Graphics&) override;
    /** Forwards to pressAt(). */
    void mouseDown (const juce::MouseEvent&) override;
    /** Forwards to dragTo(). */
    void mouseDrag (const juce::MouseEvent&) override;
    /** Forwards to release(). */
    void mouseUp (const juce::MouseEvent&) override;
    /** Forwards to doubleClickAt(). */
    void mouseDoubleClick (const juce::MouseEvent&) override;
    /** Highlights the handle under the mouse. */
    void mouseMove (const juce::MouseEvent&) override;

private:
    /** The square, in component coordinates, in which the visible part of the plane is drawn. */
    juce::Rectangle<float> planeBounds() const;
    /** Component coordinates -> plane coordinates (metres). */
    juce::Point<double> fromView (juce::Point<float> p) const;
    /** Current position of a handle in metres. */
    juce::Point<double> position (const Handle& handle) const;
    /** The x or y (`axis`) parameter of a source or speaker handle. */
    juce::RangedAudioParameter* parameter (const Handle& handle, char axis) const;

    /** A zoom state. */
    struct View
    {
        juce::Point<double> centre; ///< Centre in metres.
        double half;                ///< Half the width in metres.
    };
    /** The view that just shows all speakers in use, if there are any. */
    std::optional<View> fittedView() const;
    /** Number of sources in use. */
    int activeSources() const;
    /** Number of speakers in use. */
    int activeSpeakers() const;
    /** Timer callback: rebuilds the drawn layout when positions, counts or virtual speakers changed. */
    void timerCallback() override;

    TriPanAudioProcessor& processor; ///< The processor whose parameters are shown and edited.

    std::array<juce::RangedAudioParameter*, tripan::kMaxChannels> srcXp {};      ///< Source x parameters.
    std::array<juce::RangedAudioParameter*, tripan::kMaxChannels> srcYp {};      ///< Source y parameters.
    std::array<juce::RangedAudioParameter*, tripan::kMaxChannels> srcSpreadP {}; ///< Source spread parameters.
    std::array<juce::RangedAudioParameter*, tripan::kMaxChannels> spkXp {};      ///< Speaker x parameters.
    std::array<juce::RangedAudioParameter*, tripan::kMaxChannels> spkYp {};      ///< Speaker y parameters.

    std::vector<tripan::Vec2> triangulatedSpeakers;            ///< Speakers the drawn layout was built from.
    std::vector<tripan::VirtualSpeaker> triangulatedVirtual;   ///< Virtual speakers the drawn layout was built from.
    std::vector<tripan::VirtualSpeaker> virtualSpeakers;       ///< Virtual speakers in effect, refreshed by the timer.
    std::unique_ptr<tripan::SpeakerLayout> triangulation;      ///< The layout that is drawn.
    size_t lastStateHash = 0;                                  ///< Hash of what was last drawn, to skip repaints.
    bool showTriangulation = true;                             ///< Whether the triangles are drawn.
    double viewHalf = (double) tripan::params::kCoordRange;    ///< Half extent of the view in metres, see zoomIn().
    juce::Point<double> viewCentre;                            ///< Centre of the view in metres.

    std::optional<Handle> dragging;     ///< The handle being dragged, if any.
    bool draggingSpread = false;        ///< The drag changes the spread of the source instead of moving it.
    float lastSpreadDragY = 0.0f;       ///< Mouse y at the previous step of a spread drag.
    std::optional<Handle> hovered;      ///< The handle under the mouse, if any.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FieldComponent)
};
