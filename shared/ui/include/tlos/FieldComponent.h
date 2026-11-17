// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file FieldComponent.h
    @brief The XY pad: draws sources, speakers, an optional plugin overlay and links, and edits positions by mouse. */

#pragma once

#include "tlos/SpatialProcessor.h"
#include "tlos/Parameters.h"
#include "tlos/SourceGains.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace tlos
{

/** Top-down view of the plane: speakers (squares), sources (circles), the links from each source to the
    speakers it feeds, and whatever the plugin draws underneath (TriPan: the triangulation). Handles can be dragged.

    What is specific to a panning method is left to the plugin's subclass through three hooks:
    updateOverlay() (follow the layout), paintOverlay() (draw under the sources) and getLinkGains()
    (which speakers a source feeds, and how much). Everything else (dragging, groups, selection,
    spread, scale, rotate, zoom, grid) is the same for all plugins. */
class FieldComponent : public juce::Component,
                       public juce::SettableTooltipClient,
                       private juce::Timer
{
public:
    /** Creates the pad and starts a 30 Hz timer that follows the parameters. */
    explicit FieldComponent (SpatialProcessor& processor);

    /** Stops the timer. */
    ~FieldComponent() override;

    /** A source, a speaker, or (isVirtual, with isSource false) a virtual speaker from the manual list. */
    struct Handle
    {
        bool isSource;           ///< True for a source, false for a speaker.
        int index;               ///< Zero-based index within its kind.
        bool isVirtual = false;  ///< True for a manual virtual speaker (then isSource is false).
    };

    /** Shows or hides what the plugin draws under the sources (see paintOverlay()). */
    void setShowOverlay (bool shouldShow);

    /** @return Whether the plugin's overlay is drawn. */
    bool getShowOverlay() const noexcept { return showOverlay; }

    /** Shows or hides the lines from each source to the speakers it feeds (thickness and brightness follow the gain). */
    void setShowLinks (bool shouldShow);

    /** @return Whether the source-to-speaker lines are drawn. */
    bool getShowLinks() const noexcept { return showLinks; }

    /** Radius in pixels of a source's inner circle. This is also its mouse target: the (semi-transparent)
        spread circle around it is only a display and never takes part in mouse interaction. */
    static constexpr float kSourceRadius = 9.0f;

    /** What a drag that starts on a source does. The modifiers are read when the button goes down and apply to the
        whole drag. "Group" means the sources that move with the one you grab: its group, or the selection if
        it is part of one.

        | Modifiers (macOS / Windows)     | Mode           | Effect                                                   |
        |---------------------------------|----------------|----------------------------------------------------------|
        | none                            | Move           | moves the whole group                                    |
        | Cmd / Ctrl                      | MoveOne        | moves the grabbed source only                            |
        | Option / Alt                    | SpreadGroup    | changes the spread of the whole group                    |
        | Option+Cmd / Alt+Ctrl           | SpreadOne      | changes the spread of the grabbed source only            |
        | Option+Shift / Alt+Shift        | Scale          | drag up = group members move apart, down = closer        |
        | Cmd+Shift / Ctrl+Shift          | Rotate         | up = counter-clockwise, down = clockwise (around the centre of gravity) |

        Spread, Scale and Rotate are vertical drags (sideways movement does nothing). */
    enum class DragMode
    {
        Move,          ///< Move the group (plain drag).
        MoveOne,       ///< Move one source (Cmd).
        SpreadGroup,   ///< Change the spread of the group (Option).
        SpreadOne,     ///< Change the spread of one source (Option+Cmd).
        Scale,         ///< Change the distances in the group (Option+Shift).
        Rotate         ///< Rotate the group (Cmd+Shift).
    };

    /** @return The mode that the modifiers select (see DragMode). */
    static DragMode dragModeFor (const juce::ModifierKeys& mods) noexcept;

    /** @return Whether the modifiers change a spread (Option, with or without Cmd; not with Shift alone). Option +
        double-click resets the spread to 0 (the group's, or with Cmd the grabbed source's). */
    static bool isSpreadModifier (const juce::ModifierKeys& mods) noexcept;

    /** @return Whether the modifiers move one source alone: Cmd (Ctrl on Windows) without Option or Shift. */
    static bool isSingleSourceModifier (const juce::ModifierKeys& mods) noexcept;

    /** Sensitivity of the vertical drags. Spread: one pixel is the same distance as on the pad. Scale: each pixel
        multiplies the distances by exp(0.005), about 0.5 %. Rotate: one pixel is one degree. */
    static constexpr double kScalePerPixel = 0.005;                           ///< Natural log of the scale change per pixel.
    static constexpr double kRotationPerPixel = 3.14159265358979323846 / 180.0; ///< Radians per pixel.
    static constexpr double kMinScale = 0.05;                                 ///< Smallest scale factor of a drag (5 %).

    /** @return The colour of a source: its own colour, or, if it is in a group, the own colour of the member
        with the lowest number, so that a group is drawn in one colour and ungrouping gives the individual colours back. */
    static juce::Colour sourceColour (int source, const tlos::SourceGroups& groups);

    /// @name Selection and groups
    /// Clicking a source selects it and the rest of its group; Shift-click adds or removes a source (with
    /// its group); dragging a rectangle on empty space selects what is inside it (Shift adds to the
    /// selection). Dragging a selected source moves the whole selection. The selection is not saved;
    /// the groups are (see SpatialProcessor::getSourceGroups()).
    /// @{
    /** @return The selected sources (zero-based, ascending). */
    const std::vector<int>& getSelection() const noexcept { return selection; }
    /** Replaces the selection. Numbers outside the sources in use are dropped. */
    void setSelection (std::vector<int> sources);
    /** Selects nothing. */
    void clearSelection() { setSelection ({}); }
    /** @return The sources that move when `source` is dragged: the selection if it is part of it, otherwise
        its group (just the source if it has none). Only sources in use are included. */
    std::vector<int> movingSet (int source) const;
    /** @return Whether Group would do something: sources are set to Edit and the selection holds at least
        two sources that are not all in one group already. */
    bool canGroupSelection() const;
    /** @return Whether Ungroup would do something: sources are set to Edit and a selected source is in a group. */
    bool canUngroupSelection() const;
    /** Groups the selection (merging the groups it touches). @return false if nothing was done. */
    bool groupSelection();
    /** Dissolves the groups of the selected sources. @return false if nothing was done. */
    bool ungroupSelection();
    /** @return The rectangle of an ongoing selection drag in component coordinates, if there is one. */
    std::optional<juce::Rectangle<float>> getMarquee() const { return marquee; }
    /** Called whenever the selection or the groups changed (message thread). */
    std::function<void()> onSelectionChanged;
    /// @}

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

    /** Zoom. The full view shows the whole room (+-10 m by default, see SpatialProcessor::getRoomHalfExtent());
        it comes back by itself when the room size is changed. Zoom in moves a step towards the area that
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

    /** Current position of a handle in metres. */
    juce::Point<double> position (const Handle& handle) const;

    /** Plane coordinates (metres) -> component coordinates. */
    juce::Point<float> toView (double x, double y) const;
    /** Topmost handle within grab distance of p (component coordinates), sources before speakers. Handles that
        the Edit/View menus lock are not found, unless `ignoreEditModes` is set (the context menu works on them too). */
    std::optional<Handle> findHandleAt (juce::Point<float> p, bool ignoreEditModes = false) const;
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

    /// @name Mute, solo and exclude
    /// Right-click a source or a speaker for a menu with Mute, Solo and (speakers) Exclude from panning. They work
    /// whatever the Edit/View menus say. Mute and solo are host parameters; exclude is saved with the session.
    /// @{
    /** Toggles the mute switch of a source or speaker (inside a change gesture). Virtual speakers have none. */
    void toggleMute (const Handle& handle);
    /** Toggles the solo switch of a source or speaker (inside a change gesture). Virtual speakers have none. */
    void toggleSolo (const Handle& handle);
    /** Toggles whether speaker `index` (zero-based) is excluded from the panning. */
    void toggleExclude (int speaker);
    /** Shows the menu for a handle at the mouse position. */
    void showHandleMenu (const Handle& handle);
    /// @}

    /** Draws grid, overlay, group bands, spread circles, links, speakers and sources. */
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
    /** Cmd+G groups the selection, Cmd+Shift+G ungroups it, Escape clears the selection. */
    bool keyPressed (const juce::KeyPress&) override;
    /** Adds the group of the hovered source to the tooltip. */
    juce::String getTooltip() override;

    /** The square, in component coordinates, in which the visible part of the plane is drawn (the pad's border). */
    juce::Rectangle<float> planeBounds() const;

protected:
    /** Hook, called by the timer (and refresh()) with the speakers that take part in the panning (the ones in
        use that are not excluded, with their output channels) and the virtual speakers in effect: follow them, for
        example by rebuilding a drawn layout when they changed. The default does nothing.
        Not called during construction; a subclass calls refresh() at the end of its own constructor. */
    virtual void updateOverlay (const IncludedSpeakers& speakers, const std::vector<VirtualSpeaker>& virtualSpeakers);

    /** Hook: draws what lies under the group bands and spread circles, if the overlay is switched on
        (setShowOverlay()). Clipped to the plane; use toView() for coordinates. The default draws nothing. */
    virtual void paintOverlay (juce::Graphics& g);

    /** Hook: the speakers a source at `position` with the given spread feeds and their gains (indexed by output
        channel), drawn as links when "Show links" is on. The default returns no speakers (no links). */
    virtual SourceGains getLinkGains (int source, Vec2 position, double spread) const;

    SpatialProcessor& processor; ///< The processor whose parameters are shown and edited.

private:
    /** Component coordinates -> plane coordinates (metres). */
    juce::Point<double> fromView (juce::Point<float> p) const;
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
    /** The room half extent in metres (what the pad shows when fully zoomed out, and where dragged things stop). */
    double roomHalf() const { return processor.getRoomHalfExtent(); }
    /** Number of sources in use. */
    int activeSources() const;
    /** Number of speakers in use. */
    int activeSpeakers() const;
    /** Timer callback: rebuilds the drawn layout when positions, counts or virtual speakers changed. */
    void timerCallback() override;

    std::array<juce::RangedAudioParameter*, tlos::kMaxChannels> srcXp {};      ///< Source x parameters.
    std::array<juce::RangedAudioParameter*, tlos::kMaxChannels> srcYp {};      ///< Source y parameters.
    std::array<juce::RangedAudioParameter*, tlos::kMaxChannels> srcSpreadP {}; ///< Source spread parameters.
    std::array<juce::RangedAudioParameter*, tlos::kMaxChannels> spkXp {};      ///< Speaker x parameters.
    std::array<juce::RangedAudioParameter*, tlos::kMaxChannels> spkYp {};      ///< Speaker y parameters.

    std::vector<VirtualSpeaker> virtualSpeakers;               ///< Virtual speakers in effect, refreshed by the timer.
    std::array<bool, tlos::kMaxChannels> excludedSpeakers {};  ///< Excluded speakers, refreshed by the timer.
    std::array<std::atomic<float>*, tlos::kMaxChannels> srcMuteRaw {};  ///< Raw values of the source mute switches.
    std::array<std::atomic<float>*, tlos::kMaxChannels> srcSoloRaw {};  ///< Raw values of the source solo switches.
    std::array<std::atomic<float>*, tlos::kMaxChannels> spkMuteRaw {};  ///< Raw values of the speaker mute switches.
    std::array<std::atomic<float>*, tlos::kMaxChannels> spkSoloRaw {};  ///< Raw values of the speaker solo switches.
    size_t lastStateHash = 0;                                  ///< Hash of what was last drawn, to skip repaints.
    bool showOverlay = true;                                   ///< Whether the plugin's overlay is drawn.
    bool showLinks = true;                                     ///< Whether the source-to-speaker lines are drawn.
    double viewHalf = params::kDefaultRoomHalf;        ///< Half extent of the view in metres, see zoomIn().
    double lastRoomHalf = params::kDefaultRoomHalf;    ///< The room half extent the view was last set for.
    juce::Point<double> viewCentre;                            ///< Centre of the view in metres.

    std::optional<Handle> dragging;     ///< The handle being dragged, if any.
    bool draggingSpread = false;        ///< The drag changes spreads instead of moving sources.
    DragMode dragMode = DragMode::Move; ///< What the drag in progress does.
    float lastSpreadDragY = 0.0f;       ///< Mouse y at the previous step of a vertical drag (spread, scale, rotate).
    tlos::Vec2 dragCentre;            ///< Centre of gravity of the dragged sources at the press (scale, rotate).
    double dragScale = 1.0;             ///< Scale factor of the drag in progress.
    double dragAngle = 0.0;             ///< Rotation in radians of the drag in progress (counter-clockwise positive).
    std::optional<Handle> hovered;      ///< The handle under the mouse, if any.

    std::vector<int> selection;                 ///< Selected sources, ascending.
    tlos::SourceGroups groups;                ///< Source groups as last read from the processor.
    std::vector<int> dragSources;               ///< Sources being moved or having their spread changed.
    std::vector<tlos::Vec2> dragStart;        ///< Their positions (or, for spread, spreads in x) at the press.
    tlos::Vec2 dragGrabStart;                 ///< Position of the grabbed source at the press.
    std::optional<juce::Rectangle<float>> marquee; ///< Selection rectangle while dragging on empty space.
    juce::Point<float> marqueeOrigin;           ///< Where the selection rectangle started.
    std::vector<int> marqueeBase;               ///< Selection to extend (Shift) or empty.
    juce::String baseTooltip;                   ///< Tooltip text without the group note.

    /** Toggles a mute or solo switch parameter inside a change gesture. */
    void toggleSwitch (const juce::String& parameterId);

    /** Sets the x and y parameters of `dragSources` to `positions` (same order). */
    void writeDragPositions (const std::vector<tlos::Vec2>& positions);
    /** Moves `dragSources` so that the grabbed source is at `p` (component coordinates), as far as the pad allows. */
    void moveSelectionTo (juce::Point<float> p);
    /** Selects what the rectangle holds (with the groups) in addition to marqueeBase. */
    void updateMarqueeSelection();
    /** Tells the owner that the selection or groups changed. */
    void notifySelectionChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FieldComponent)
};
}
