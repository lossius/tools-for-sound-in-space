#pragma once

#include "PluginProcessor.h"
#include "Parameters.h"
#include "tripan/Triangulation.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

/** Top-down view of the plane: speakers (squares), sources (circles), the Delaunay triangulation,
    and the links from each source to the speakers it feeds. Handles can be dragged. */
class FieldComponent final : public juce::Component,
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

    std::array<juce::RangedAudioParameter*, tripan::kMaxChannels> srcXp {}, srcYp {}, spkXp {}, spkYp {};

    std::vector<tripan::Vec2> triangulatedSpeakers;
    std::vector<tripan::Vec2> triangulatedVirtual;
    std::vector<tripan::Vec2> virtualSpeakers; // as in effect, refreshed by the timer
    std::unique_ptr<tripan::Triangulation> triangulation;
    size_t lastStateHash = 0;
    bool showTriangulation = true;
    double viewHalf = (double) tripan::params::kCoordRange; // see zoomIn()
    juce::Point<double> viewCentre;

    std::optional<Handle> dragging;
    std::optional<Handle> hovered;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FieldComponent)
};
