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
    explicit FieldComponent (TriPanAudioProcessor&);
    ~FieldComponent() override;

    /** A source, a speaker, or (isVirtual, with isSource false) a virtual speaker from the manual list. */
    struct Handle
    {
        bool isSource;
        int index;
        bool isVirtual = false;
    };

    void setShowTriangulation (bool shouldShow);

    /** Radius in pixels of a source's inner circle. This is also its mouse target: the (semi-transparent)
        spread circle around it is only a display and never takes part in mouse interaction. */
    static constexpr float kSourceRadius = 9.0f;

    /** Spread: hold Option (Alt on Windows) and drag a source vertically. Moving the mouse up
        increases the spread, moving it down decreases it, sideways movement has no effect. The change
        is relative to where the drag starts and is limited to 0..10 m at every step, so dragging
        down past zero and back up increases the spread again at once. Option + double-click resets
        the spread to 0. Only Option is used (not Cmd). */
    static bool isSpreadModifier (const juce::ModifierKeys&) noexcept;
    double getSpread (int source) const;
    /** Sets the spread of a source in metres (limited to 0..10). Does nothing if sources are set to View. */
    void setSpread (int source, double metres);

    // The mouse handlers call these, so that they can be tested without real mouse events.
    void pressAt (juce::Point<float> p, const juce::ModifierKeys& mods);
    void dragTo (juce::Point<float> p);
    void release();
    void doubleClickAt (juce::Point<float> p, const juce::ModifierKeys& mods);

    /** Zoom. The full view shows the whole +-10 m plane. Zoom in moves a step towards the area that
        contains all speakers in use (and stops there); zoom out moves a step back towards the full
        view. The view is fixed once set, so it does not shift while speakers are dragged. */
    void zoomIn();
    void zoomOut();
    bool canZoomIn() const;
    bool canZoomOut() const;
    /** Half the width of the visible area in metres (10 = the entire plane) and its centre. */
    double getViewHalfExtent() const noexcept { return viewHalf; }
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
    bool addVirtualSpeakerAt (juce::Point<float> p);
    bool removeVirtualSpeaker (int index);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> planeBounds() const;
    juce::Point<double> fromView (juce::Point<float> p) const;
    juce::Point<double> position (const Handle&) const;
    juce::RangedAudioParameter* parameter (const Handle&, char axis) const;
    struct View
    {
        juce::Point<double> centre;
        double half;
    };
    /** The view that just shows all speakers in use, if there are any. */
    std::optional<View> fittedView() const;
    int activeSources() const;
    int activeSpeakers() const;
    void timerCallback() override;

    TriPanAudioProcessor& processor;

    std::array<juce::RangedAudioParameter*, tripan::kMaxChannels> srcXp {}, srcYp {}, srcSpreadP {}, spkXp {}, spkYp {};

    std::vector<tripan::Vec2> triangulatedSpeakers;
    std::vector<tripan::VirtualSpeaker> triangulatedVirtual;
    std::vector<tripan::VirtualSpeaker> virtualSpeakers; // as in effect, refreshed by the timer
    std::unique_ptr<tripan::SpeakerLayout> triangulation;
    size_t lastStateHash = 0;
    bool showTriangulation = true;
    double viewHalf = (double) tripan::params::kCoordRange; // see zoomIn()
    juce::Point<double> viewCentre;

    std::optional<Handle> dragging;
    bool draggingSpread = false; // the drag changes the spread of the source instead of moving it
    float lastSpreadDragY = 0.0f; // mouse y at the previous step of a spread drag
    std::optional<Handle> hovered;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FieldComponent)
};
