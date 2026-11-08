#include "FieldComponent.h"
#include "Parameters.h"

#include "tripan/Gains.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
constexpr double kFullHalf = tripan::params::kCoordRange; // the full view shows +-kFullHalf metres
constexpr double kZoomStep = 1.5;                         // view size ratio between two zoom levels
constexpr double kMinHalf = 2.0;                          // never zoom in closer than +-2 m
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
    if (h.isVirtual)
    {
        const auto i = (size_t) h.index;
        return i < virtualSpeakers.size() ? juce::Point<double> (virtualSpeakers[i].position.x, virtualSpeakers[i].position.y) : juce::Point<double>();
    }

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
    return { r.getX() + r.getWidth() * (float) ((x - viewCentre.x) / (2.0 * viewHalf) + 0.5),
             r.getBottom() - r.getHeight() * (float) ((y - viewCentre.y) / (2.0 * viewHalf) + 0.5) };
}

juce::Point<double> FieldComponent::fromView (juce::Point<float> p) const
{
    const auto r = planeBounds();
    return { viewCentre.x + (((double) (p.x - r.getX()) / r.getWidth()) - 0.5) * 2.0 * viewHalf,
             viewCentre.y + (((double) (r.getBottom() - p.y) / r.getHeight()) - 0.5) * 2.0 * viewHalf };
}

std::optional<FieldComponent::Handle> FieldComponent::findHandleAt (juce::Point<float> p) const
{
    std::optional<Handle> best;
    float bestDistance = kGrabRadius;

    const auto consider = [&] (bool isSource, int count, bool isVirtual = false)
    {
        for (int i = 0; i < count; ++i) // later indices are drawn on top, so they win ties
        {
            const Handle h { isSource, i, isVirtual };
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
    if (canEditVirtualSpeakers())    consider (false, (int) virtualSpeakers.size(), true);
    if (processor.canEditSources())  consider (true, activeSources());
    return best;
}

bool FieldComponent::canEditVirtualSpeakers() const
{
    return processor.getVirtualMode() == VirtualMode::Manual && processor.canEditSpeakers();
}

bool FieldComponent::addVirtualSpeakerAt (juce::Point<float> p)
{
    auto list = processor.getManualVirtualSpeakers();
    if (! canEditVirtualSpeakers() || (int) list.size() >= TriPanAudioProcessor::kMaxVirtualSpeakers)
        return false;

    const auto pos = fromView (p);
    const float limit = tripan::params::kCoordRange;
    list.push_back ({ (double) juce::jlimit (-limit, limit, (float) pos.x), (double) juce::jlimit (-limit, limit, (float) pos.y) });
    processor.setManualVirtualSpeakers (list);
    refresh();
    return true;
}

bool FieldComponent::removeVirtualSpeaker (int index)
{
    auto list = processor.getManualVirtualSpeakers();
    if (! canEditVirtualSpeakers() || index < 0 || index >= (int) list.size())
        return false;

    list.erase (list.begin() + index);
    processor.setManualVirtualSpeakers (list);
    refresh();
    return true;
}

void FieldComponent::moveHandle (const Handle& h, juce::Point<float> p)
{
    if (h.isVirtual)
    {
        auto list = processor.getManualVirtualSpeakers();
        if (! canEditVirtualSpeakers() || h.index < 0 || h.index >= (int) list.size())
            return;

        const auto pos = fromView (p);
        const float limit = tripan::params::kCoordRange;
        list[(size_t) h.index] = { (double) juce::jlimit (-limit, limit, (float) pos.x), (double) juce::jlimit (-limit, limit, (float) pos.y) };
        processor.setManualVirtualSpeakers (list);
        refresh();
        return;
    }

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

std::optional<FieldComponent::View> FieldComponent::fittedView() const
{
    const int m = activeSpeakers();
    if (m <= 0)
        return std::nullopt;

    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
    for (int i = 0; i < m; ++i)
    {
        const auto pos = position ({ false, i });
        minX = std::min (minX, pos.x);  maxX = std::max (maxX, pos.x);
        minY = std::min (minY, pos.y);  maxY = std::max (maxY, pos.y);
    }

    View v;
    v.centre = { 0.5 * (minX + maxX), 0.5 * (minY + maxY) };
    v.half = std::clamp (std::max (maxX - minX, maxY - minY) * 0.5 * 1.1 + 0.5, kMinHalf, kFullHalf);
    return v;
}

bool FieldComponent::canZoomIn() const
{
    const auto fit = fittedView();
    return fit && fit->half < viewHalf - 1e-6;
}

bool FieldComponent::canZoomOut() const
{
    return viewHalf < kFullHalf - 1e-6;
}

void FieldComponent::zoomIn()
{
    if (! canZoomIn())
        return;

    const auto fit = *fittedView();
    const double newHalf = std::max (fit.half, viewHalf / kZoomStep);
    const double fraction = (viewHalf - newHalf) / (viewHalf - fit.half); // how far to the fitted view
    viewCentre += (fit.centre - viewCentre) * fraction;
    viewHalf = newHalf;
    repaint();
}

void FieldComponent::zoomOut()
{
    if (! canZoomOut())
        return;

    const double newHalf = std::min (kFullHalf, viewHalf * kZoomStep);
    const double fraction = (newHalf - viewHalf) / (kFullHalf - viewHalf); // how far to the full view
    viewCentre = viewCentre * (1.0 - fraction);
    viewHalf = newHalf;
    if (viewHalf >= kFullHalf - 1e-9)
        viewCentre = {};
    repaint();
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

    virtualSpeakers = processor.getVirtualSpeakers();
    mix ((double) processor.getVirtualMode());
    for (const auto& v : virtualSpeakers)
    {
        mix (v.position.x);
        mix (v.position.y);
        mix (v.strength);
    }

    const auto samePosition = [] (const tripan::Vec2& p, const tripan::Vec2& q) { return juce::exactlyEqual (p.x, q.x) && juce::exactlyEqual (p.y, q.y); };
    const auto sameVirtual = [&] (const tripan::VirtualSpeaker& p, const tripan::VirtualSpeaker& q)
    {
        return samePosition (p.position, q.position) && juce::exactlyEqual (p.strength, q.strength);
    };

    const bool changed = triangulation == nullptr
                      || speakers.size() != triangulatedSpeakers.size()
                      || ! std::equal (speakers.begin(), speakers.end(), triangulatedSpeakers.begin(), samePosition)
                      || virtualSpeakers.size() != triangulatedVirtual.size()
                      || ! std::equal (virtualSpeakers.begin(), virtualSpeakers.end(), triangulatedVirtual.begin(), sameVirtual);

    if (changed)
    {
        triangulation = std::make_unique<tripan::SpeakerLayout> (speakers, virtualSpeakers);
        triangulatedSpeakers = std::move (speakers);
        triangulatedVirtual = virtualSpeakers;
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

    // Grid. The spacing follows the zoom: minor lines every 1 / 0.5 / 0.25 m, stronger major lines
    // every 5 / 2 / 1 m, and the axes (x = 0, y = 0) strongest. Major lines are labelled at the edges.
    const double minorStep = viewHalf >= 5.0 ? 1.0 : (viewHalf >= 2.5 ? 0.5 : 0.25);
    const double majorStep = viewHalf >= 5.0 ? 5.0 : (viewHalf >= 2.5 ? 2.0 : 1.0);

    const auto formatMetres = [] (double v) { return juce::String (v, juce::approximatelyEqual (std::fmod (std::abs (v), 1.0), 0.0) ? 0 : 1) + " m"; };

    g.saveState();
    g.reduceClipRegion (plane.toNearestInt());

    const auto drawLines = [&] (bool vertical)
    {
        const double centre = vertical ? viewCentre.x : viewCentre.y;
        const long first = (long) std::ceil ((centre - viewHalf) / minorStep - 1e-9);
        const long last  = (long) std::floor ((centre + viewHalf) / minorStep + 1e-9);

        for (long n = first; n <= last; ++n)
        {
            const double v = (double) n * minorStep;
            const bool axis = n == 0;
            const bool major = std::abs (std::remainder (v, majorStep)) < 1e-9;

            g.setColour (juce::Colours::white.withAlpha (axis ? 0.35f : major ? 0.16f : 0.06f));
            const auto a = vertical ? toView (v, viewCentre.y - viewHalf) : toView (viewCentre.x - viewHalf, v);
            const auto b = vertical ? toView (v, viewCentre.y + viewHalf) : toView (viewCentre.x + viewHalf, v);
            g.drawLine (a.x, a.y, b.x, b.y, axis ? 1.5f : 1.0f);

            // Labels sit at the edges; skip lines right at an edge so that labels never collide in the corners.
            const bool nearEdge = vertical ? (a.x < plane.getX() + 26.0f || a.x > plane.getRight() - 26.0f)
                                           : (a.y < plane.getY() + 16.0f || a.y > plane.getBottom() - 20.0f);
            if (major && ! nearEdge)
            {
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.setFont (juce::FontOptions (11.0f));
                if (vertical)
                    g.drawText (formatMetres (v), juce::Rectangle<float> (a.x - 25.0f, plane.getBottom() - 15.0f, 50.0f, 13.0f), juce::Justification::centred);
                else
                    g.drawText (formatMetres (v), juce::Rectangle<float> (plane.getX() + 4.0f, a.y - 14.0f, 50.0f, 13.0f), juce::Justification::left);
            }
        }
    };
    drawLines (true);
    drawLines (false);

    g.restoreState();

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("front", juce::Rectangle<float> (plane.getCentreX() - 30.0f, plane.getY() - 18.0f, 60.0f, 16.0f),
                juce::Justification::centred);
    g.drawText ("back", juce::Rectangle<float> (plane.getCentreX() - 30.0f, plane.getBottom() + 2.0f, 60.0f, 16.0f),
                juce::Justification::centred);

    g.saveState();
    g.reduceClipRegion (plane.toNearestInt());

    const int n = activeSources(), m = activeSpeakers();

    // Triangulation
    if (showTriangulation && triangulation != nullptr)
    {
        g.setColour (juce::Colour (0xff7aa2c8).withAlpha (0.55f));
        const auto& full = triangulation->full(); // the triangulation with all virtual speakers
        const auto pts = full.speakers();
        for (const auto& t : full.triangles())
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

    // Virtual speakers: hollow squares. They are not outputs; their gain goes to the real speakers around them.
    for (int i = 0; i < (int) virtualSpeakers.size(); ++i)
    {
        // Automatic virtual speakers fade in and out with their strength; manual ones are always solid.
        const float strength = (float) juce::jlimit (0.15, 1.0, virtualSpeakers[(size_t) i].strength);
        const auto c = toView (virtualSpeakers[(size_t) i].position.x, virtualSpeakers[(size_t) i].position.y);
        const bool hot = (hovered && hovered->isVirtual && hovered->index == i) || (dragging && dragging->isVirtual && dragging->index == i);
        const auto r = juce::Rectangle<float> (14.0f, 14.0f).withCentre (c);
        g.setColour (juce::Colour (0xff15181d).withAlpha (0.85f * strength));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour ((hot ? juce::Colours::white : juce::Colour (0xffe6b450)).withAlpha (strength));
        g.drawRoundedRectangle (r, 2.0f, hot ? 2.0f : 1.5f);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText ("v" + juce::String (i + 1), r, juce::Justification::centred);
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
        if (! dragging->isVirtual)
        {
            parameter (*dragging, 'x')->beginChangeGesture();
            parameter (*dragging, 'y')->beginChangeGesture();
        }
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
        if (! dragging->isVirtual)
        {
            parameter (*dragging, 'x')->endChangeGesture();
            parameter (*dragging, 'y')->endChangeGesture();
        }
        dragging.reset();
        repaint();
    }
}

void FieldComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Manual virtual speakers: double-click one to delete it, or empty space to add one there.
    if (! canEditVirtualSpeakers())
        return;

    const auto h = findHandleAt (e.position);
    if (h && h->isVirtual)
        removeVirtualSpeaker (h->index);
    else if (! h && planeBounds().contains (e.position))
        addVirtualSpeakerAt (e.position);
}

void FieldComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto h = findHandleAt (e.position);
    const bool changed = h.has_value() != hovered.has_value()
                      || (h && hovered && (h->isSource != hovered->isSource || h->isVirtual != hovered->isVirtual || h->index != hovered->index));
    hovered = h;
    setMouseCursor (h ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (changed)
        repaint();
}
