#pragma once

#include "PluginProcessor.h"
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

    struct Handle
    {
        bool isSource;
        int index;
    };

    void setShowTriangulation (bool shouldShow);

    /** Re-reads the parameters and repaints if anything changed (also called by a 30 Hz timer). */
    void refresh() { timerCallback(); }

    /** Plane coordinates (metres) -> component coordinates. */
    juce::Point<float> toView (double x, double y) const;
    /** Topmost handle within grab distance of p (component coordinates), sources before speakers. */
    std::optional<Handle> findHandleAt (juce::Point<float> p) const;
    /** Sets the handle's parameters from a component-coordinate position (clamped to the range). */
    void moveHandle (const Handle& handle, juce::Point<float> p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> planeBounds() const;
    juce::Point<double> fromView (juce::Point<float> p) const;
    juce::Point<double> position (const Handle&) const;
    juce::RangedAudioParameter* parameter (const Handle&, char axis) const;
    int activeSources() const;
    int activeSpeakers() const;
    void timerCallback() override;

    TriPanAudioProcessor& processor;

    std::array<juce::RangedAudioParameter*, tripan::kMaxChannels> srcXp {}, srcYp {}, spkXp {}, spkYp {};

    std::vector<tripan::Vec2> triangulatedSpeakers;
    std::unique_ptr<tripan::Triangulation> triangulation;
    size_t lastStateHash = 0;
    bool showTriangulation = true;

    std::optional<Handle> dragging;
    std::optional<Handle> hovered;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FieldComponent)
};
