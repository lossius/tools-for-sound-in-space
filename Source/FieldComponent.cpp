#include "FieldComponent.h"
#include "Parameters.h"

#include "tripan/Gains.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
constexpr float kViewRange = tripan::params::kCoordRange; // metres shown on each side of the origin
constexpr float kGrabRadius = 13.0f;
constexpr float kMargin = 22.0f;

juce::Colour sourceColour (int index)
{
    const float hue = std::fmod ((float) index * 0.61803398875f, 1.0f);
    return juce::Colour::fromHSV (hue, 0.55f, 0.98f, 1.0f);
}
}

FieldComponent::FieldComponent (TriPanAudioProcessor& p)
    : processor (p)
{
    auto& apvts = processor.getValueTreeState();
    for (int i = 0; i < tripan::kMaxChannels; ++i)
    {
        srcXp[(size_t) i] = apvts.getParameter (tripan::params::sourceId (i, 'x'));
        srcYp[(size_t) i] = apvts.getParameter (tripan::params::sourceId (i, 'y'));
        spkXp[(size_t) i] = apvts.getParameter (tripan::params::speakerId (i, 'x'));
        spkYp[(size_t) i] = apvts.getParameter (tripan::params::speakerId (i, 'y'));
    }

    timerCallback();
    startTimerHz (30);
}

FieldComponent::~FieldComponent()
{
    stopTimer();
}

void FieldComponent::setShowTriangulation (bool shouldShow)
{
    showTriangulation = shouldShow;
    repaint();
}

int FieldComponent::activeSources() const  { return processor.getActiveSources(); }
int FieldComponent::activeSpeakers() const { return processor.getActiveSpeakers(); }

juce::RangedAudioParameter* FieldComponent::parameter (const Handle& h, char axis) const
{
    const auto i = (size_t) h.index;
    if (h.isSource) return axis == 'x' ? srcXp[i] : srcYp[i];
    return axis == 'x' ? spkXp[i] : spkYp[i];
}

juce::Point<double> FieldComponent::position (const Handle& h) const
{
    const auto read = [] (juce::RangedAudioParameter* prm) { return (double) prm->convertFrom0to1 (prm->getValue()); };
    return { read (parameter (h, 'x')), read (parameter (h, 'y')) };
}

juce::Rectangle<float> FieldComponent::planeBounds() const
{
    const auto area = getLocalBounds().toFloat().reduced (kMargin);
    const float side = std::min (area.getWidth(), area.getHeight());
    return juce::Rectangle<float> (side, side).withCentre (getLocalBounds().toFloat().getCentre());
}

juce::Point<float> FieldComponent::toView (double x, double y) const
{
    const auto r = planeBounds();
    return { r.getX() + r.getWidth() * (float) ((x + kViewRange) / (2.0 * kViewRange)),
             r.getBottom() - r.getHeight() * (float) ((y + kViewRange) / (2.0 * kViewRange)) };
}

juce::Point<double> FieldComponent::fromView (juce::Point<float> p) const
{
    const auto r = planeBounds();
    return { ((double) (p.x - r.getX()) / r.getWidth()) * 2.0 * kViewRange - kViewRange,
             ((double) (r.getBottom() - p.y) / r.getHeight()) * 2.0 * kViewRange - kViewRange };
}

std::optional<FieldComponent::Handle> FieldComponent::findHandleAt (juce::Point<float> p) const
{
    std::optional<Handle> best;
    float bestDistance = kGrabRadius;

    const auto consider = [&] (bool isSource, int count)
    {
        for (int i = 0; i < count; ++i) // later indices are drawn on top, so they win ties
        {
            const Handle h { isSource, i };
            const auto pos = position (h);
            const float d = toView (pos.x, pos.y).getDistanceFrom (p);
            if (d <= bestDistance)
            {
                bestDistance = d;
                best = h;
            }
        }
    };

    // Speakers first, then sources with <=, so that on a tie the source (drawn on top) wins.
    // Handles that the current edit mode locks are not grabbable.
    if (processor.canEditSpeakers()) consider (false, activeSpeakers());
    if (processor.canEditSources())  consider (true, activeSources());
    return best;
}

void FieldComponent::moveHandle (const Handle& h, juce::Point<float> p)
{
    if (h.isSource ? ! processor.canEditSources() : ! processor.canEditSpeakers())
        return;

    const auto pos = fromView (p);
    const auto set = [] (juce::RangedAudioParameter* prm, double v)
    {
        const float clamped = juce::jlimit (-tripan::params::kCoordRange, tripan::params::kCoordRange, (float) v);
        prm->setValueNotifyingHost (prm->convertTo0to1 (clamped));
    };

    set (parameter (h, 'x'), pos.x);
    set (parameter (h, 'y'), pos.y);
}

void FieldComponent::timerCallback()
{
    const int n = activeSources(), m = activeSpeakers();

    size_t hash = std::hash<int> {} (n * 1000 + m);
    const auto mix = [&] (double v) { hash = hash * 1000003u ^ std::hash<double> {} (v); };

    std::vector<tripan::Vec2> speakers;
    speakers.reserve ((size_t) m);
    for (int i = 0; i < m; ++i)
    {
        const auto pos = position ({ false, i });
        speakers.push_back ({ pos.x, pos.y });
        mix (pos.x);
        mix (pos.y);
    }
    for (int i = 0; i < n; ++i)
    {
        const auto pos = position ({ true, i });
        mix (pos.x);
        mix (pos.y);
    }

    if (triangulation == nullptr || speakers.size() != triangulatedSpeakers.size()
        || ! std::equal (speakers.begin(), speakers.end(), triangulatedSpeakers.begin(),
                         [] (const tripan::Vec2& a, const tripan::Vec2& b) { return juce::exactlyEqual (a.x, b.x) && juce::exactlyEqual (a.y, b.y); }))
    {
        triangulation = std::make_unique<tripan::Triangulation> (speakers);
        triangulatedSpeakers = std::move (speakers);
    }

    if (hash != lastStateHash)
    {
        lastStateHash = hash;
        repaint();
    }
}

void FieldComponent::paint (juce::Graphics& g)
{
    const auto plane = planeBounds();

    g.setColour (juce::Colour (0xff15181d));
    g.fillRoundedRectangle (plane, 6.0f);

    // Grid: every metre faint, every 5 m stronger, axes strongest.
    for (int k = -(int) kViewRange; k <= (int) kViewRange; ++k)
    {
        const bool axis = k == 0;
        const bool major = k % 5 == 0;
        g.setColour (juce::Colours::white.withAlpha (axis ? 0.35f : major ? 0.16f : 0.06f));
        const auto a = toView (k, -kViewRange), b = toView (k, kViewRange);
        const auto c = toView (-kViewRange, k), d = toView (kViewRange, k);
        g.drawLine (a.x, a.y, b.x, b.y, axis ? 1.5f : 1.0f);
        g.drawLine (c.x, c.y, d.x, d.y, axis ? 1.5f : 1.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("front", juce::Rectangle<float> (plane.getCentreX() - 30.0f, plane.getY() - 18.0f, 60.0f, 16.0f),
                juce::Justification::centred);
    g.drawText ("back", juce::Rectangle<float> (plane.getCentreX() - 30.0f, plane.getBottom() + 2.0f, 60.0f, 16.0f),
                juce::Justification::centred);
    for (int k : { -5, 5 })
    {
        const auto p = toView (k, 0);
        g.drawText (juce::String (k) + " m", juce::Rectangle<float> (p.x - 20.0f, p.y + 2.0f, 40.0f, 14.0f), juce::Justification::centred);
    }

    g.saveState();
    g.reduceClipRegion (plane.toNearestInt());

    const int n = activeSources(), m = activeSpeakers();

    // Triangulation
    if (showTriangulation && triangulation != nullptr)
    {
        g.setColour (juce::Colour (0xff7aa2c8).withAlpha (0.55f));
        const auto pts = triangulation->speakers();
        for (const auto& t : triangulation->triangles())
        {
            juce::Path path;
            const auto a = toView (pts[(size_t) t.a].x, pts[(size_t) t.a].y);
            const auto b = toView (pts[(size_t) t.b].x, pts[(size_t) t.b].y);
            const auto c = toView (pts[(size_t) t.c].x, pts[(size_t) t.c].y);
            path.startNewSubPath (a);
            path.lineTo (b);
            path.lineTo (c);
            path.closeSubPath();
            g.strokePath (path, juce::PathStrokeType (1.0f));
        }
    }

    // Links from each source to the speakers it currently feeds, weighted by gain.
    if (triangulation != nullptr)
    {
        for (int s = 0; s < n; ++s)
        {
            const auto sp = position ({ true, s });
            const auto gains = tripan::computeGains (*triangulation, { sp.x, sp.y });
            const auto from = toView (sp.x, sp.y);
            for (int i = 0; i < gains.count; ++i)
            {
                const auto spk = position ({ false, gains.speaker[(size_t) i] });
                const auto to = toView (spk.x, spk.y);
                const float gain = gains.gain[(size_t) i];
                g.setColour (sourceColour (s).withAlpha (0.15f + 0.75f * gain));
                g.drawLine (from.x, from.y, to.x, to.y, 0.8f + 2.4f * gain);
            }
        }
    }

    // Speakers
    for (int i = 0; i < m; ++i)
    {
        const auto pos = position ({ false, i });
        const auto c = toView (pos.x, pos.y);
        const bool hot = (hovered && ! hovered->isSource && hovered->index == i) || (dragging && ! dragging->isSource && dragging->index == i);
        const auto r = juce::Rectangle<float> (14.0f, 14.0f).withCentre (c);
        g.setColour (hot ? juce::Colours::white : juce::Colour (0xffdfe6ee));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (juce::Colour (0xff15181d));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), r, juce::Justification::centred);
    }

    // Sources
    for (int s = 0; s < n; ++s)
    {
        const auto pos = position ({ true, s });
        const auto c = toView (pos.x, pos.y);
        const bool hot = (hovered && hovered->isSource && hovered->index == s) || (dragging && dragging->isSource && dragging->index == s);
        const auto r = juce::Rectangle<float> (18.0f, 18.0f).withCentre (c);
        g.setColour (sourceColour (s));
        g.fillEllipse (r);
        g.setColour (hot ? juce::Colours::white : juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (r, hot ? 2.0f : 1.0f);
        g.setColour (juce::Colours::black);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (s + 1), r, juce::Justification::centred);
    }

    g.restoreState();

    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawRoundedRectangle (plane, 6.0f, 1.0f);
}

void FieldComponent::mouseDown (const juce::MouseEvent& e)
{
    dragging = findHandleAt (e.position);
    if (dragging)
    {
        parameter (*dragging, 'x')->beginChangeGesture();
        parameter (*dragging, 'y')->beginChangeGesture();
        moveHandle (*dragging, e.position);
        repaint();
    }
}

void FieldComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        moveHandle (*dragging, e.position);
}

void FieldComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        parameter (*dragging, 'x')->endChangeGesture();
        parameter (*dragging, 'y')->endChangeGesture();
        dragging.reset();
        repaint();
    }
}

void FieldComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto h = findHandleAt (e.position);
    const bool changed = h.has_value() != hovered.has_value()
                      || (h && hovered && (h->isSource != hovered->isSource || h->index != hovered->index));
    hovered = h;
    setMouseCursor (h ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (changed)
        repaint();
}
