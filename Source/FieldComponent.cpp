// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file FieldComponent.cpp
    @brief Drawing, hit-testing, dragging, spread editing, virtual speaker editing and zoom of the XY pad. */

#include "FieldComponent.h"
#include "Parameters.h"

#include "tripan/Gains.h"
#include "tripan/Groups.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
/// The full view shows +-kFullHalf metres.
constexpr double kFullHalf = tripan::params::kCoordRange;
/// View size ratio between two zoom levels.
constexpr double kZoomStep = 1.5;
/// Never zoom in closer than +-2 m.
constexpr double kMinHalf = 2.0;
/// Mouse grab distance in pixels for speakers and virtual speakers (sources use FieldComponent::kSourceRadius).
constexpr float kGrabRadius = 13.0f;
/// Space in pixels around the plane for the axis labels.
constexpr float kMargin = 22.0f;

/** Own colour of source `index`: hues spaced by the golden ratio, so neighbours are easy to tell apart. */
juce::Colour individualColour (int index)
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
        srcSpreadP[(size_t) i] = apvts.getParameter (tripan::params::spreadId (i));
        spkXp[(size_t) i] = apvts.getParameter (tripan::params::speakerId (i, 'x'));
        spkYp[(size_t) i] = apvts.getParameter (tripan::params::speakerId (i, 'y'));
    }

    baseTooltip = "Drag a source (its inner circle) to move it, with the rest of its group or selection. "
                  "Shift-click adds to the selection; drag on empty space to select several. "
                  "Cmd-drag moves one source alone. Cmd+G groups the selection, Cmd+Shift+G ungroups it.\n"
                  "Option-drag a source up or down to change its spread (up = more, down = less); "
                  "add Shift to change the whole group. Option-double-click resets the spread to 0.";
    setTooltip (baseTooltip);
    setWantsKeyboardFocus (true);

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

void FieldComponent::setShowLinks (bool shouldShow)
{
    showLinks = shouldShow;
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
    float bestDistance = 1e9f;

    // Sources are grabbed only inside their drawn (inner) circle, so that the large spread circle of one
    // source can never hide another source. Speakers have a slightly generous target.
    const auto consider = [&] (bool isSource, int count, bool isVirtual = false)
    {
        const float reach = isSource ? kSourceRadius : kGrabRadius;

        for (int i = 0; i < count; ++i) // later indices are drawn on top, so they win ties
        {
            const Handle h { isSource, i, isVirtual };
            const auto pos = position (h);
            const float d = toView (pos.x, pos.y).getDistanceFrom (p);
            if (d <= reach && d <= bestDistance)
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

bool FieldComponent::isSpreadModifier (const juce::ModifierKeys& mods) noexcept
{
    return mods.isAltDown(); // Option on macOS
}

double FieldComponent::getSpread (int source) const
{
    if (source < 0 || source >= tripan::kMaxChannels)
        return 0.0;

    auto* prm = srcSpreadP[(size_t) source];
    return (double) prm->convertFrom0to1 (prm->getValue());
}

void FieldComponent::setSpread (int source, double metres)
{
    if (source < 0 || source >= tripan::kMaxChannels || ! processor.canEditSources())
        return;

    auto* prm = srcSpreadP[(size_t) source];
    prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (0.0f, tripan::params::kMaxSpread, (float) metres)));
}

juce::Colour FieldComponent::sourceColour (int source, const tripan::SourceGroups& groups)
{
    const int id = groups.groupOf (source);
    if (id > 0)
        for (int i = 0; i < tripan::kMaxChannels; ++i)
            if (groups.groupOf (i) == id)
                return individualColour (i); // the lowest member

    return individualColour (source);
}

bool FieldComponent::isGroupSpreadModifier (const juce::ModifierKeys& mods) noexcept
{
    return mods.isAltDown() && mods.isShiftDown();
}

bool FieldComponent::isSingleSourceModifier (const juce::ModifierKeys& mods) noexcept
{
    return mods.isCommandDown() && ! mods.isAltDown();
}

void FieldComponent::notifySelectionChanged()
{
    if (onSelectionChanged)
        onSelectionChanged();
}

void FieldComponent::setSelection (std::vector<int> sources)
{
    const int n = activeSources();
    sources.erase (std::remove_if (sources.begin(), sources.end(), [n] (int s) { return s < 0 || s >= n; }), sources.end());
    std::sort (sources.begin(), sources.end());
    sources.erase (std::unique (sources.begin(), sources.end()), sources.end());

    if (sources == selection)
        return;

    selection = std::move (sources);
    repaint();
    notifySelectionChanged();
}

std::vector<int> FieldComponent::movingSet (int source) const
{
    if (std::binary_search (selection.begin(), selection.end(), source))
        return selection;

    return processor.getSourceGroups().membersOf (source, activeSources());
}

bool FieldComponent::canGroupSelection() const
{
    if (! processor.canEditSources() || selection.size() < 2)
        return false;

    auto g = processor.getSourceGroups();
    const auto before = g;
    g.group (selection);
    return ! (g == before);
}

bool FieldComponent::canUngroupSelection() const
{
    if (! processor.canEditSources())
        return false;

    const auto g = processor.getSourceGroups();
    return std::any_of (selection.begin(), selection.end(), [&] (int s) { return g.groupOf (s) > 0; });
}

bool FieldComponent::groupSelection()
{
    if (! canGroupSelection())
        return false;

    auto g = processor.getSourceGroups();
    g.group (selection);
    processor.setSourceGroups (g);
    groups = g;
    setSelection (g.membersOf (selection.front(), activeSources())); // the selection grows to the merged group
    repaint();
    notifySelectionChanged();
    return true;
}

bool FieldComponent::ungroupSelection()
{
    if (! canUngroupSelection())
        return false;

    auto g = processor.getSourceGroups();
    g.ungroup (selection);
    processor.setSourceGroups (g);
    groups = g;
    repaint();
    notifySelectionChanged();
    return true;
}

bool FieldComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode()) == 'G')
        return key.getModifiers().isShiftDown() ? ungroupSelection() : groupSelection();

    if (key == juce::KeyPress::escapeKey && ! selection.empty())
    {
        clearSelection();
        return true;
    }

    return false;
}

juce::String FieldComponent::getTooltip()
{
    if (hovered && hovered->isSource)
    {
        const int id = groups.groupOf (hovered->index);
        if (id > 0)
            return "Group " + juce::String (id) + " (" + juce::String (groups.sizeOf (id, activeSources())) + " sources)\n" + baseTooltip;
    }

    return baseTooltip;
}

void FieldComponent::moveSelectionTo (juce::Point<float> p)
{
    if (dragSources.empty() || ! processor.canEditSources())
        return;

    // The grabbed source goes to the pointer and the others keep their offsets to it; the movement is
    // limited as a whole, so that the shape never changes when a member would leave the pad.
    const auto pos = fromView (p);
    const auto delta = tripan::clampTranslation (dragStart, { pos.x - dragGrabStart.x, pos.y - dragGrabStart.y },
                                                 (double) tripan::params::kCoordRange);

    for (size_t i = 0; i < dragSources.size(); ++i)
    {
        const auto set = [] (juce::RangedAudioParameter* prm, double v)
        {
            const float clamped = juce::jlimit (-tripan::params::kCoordRange, tripan::params::kCoordRange, (float) v);
            prm->setValueNotifyingHost (prm->convertTo0to1 (clamped));
        };

        const auto src = (size_t) dragSources[i];
        set (srcXp[src], dragStart[i].x + delta.x);
        set (srcYp[src], dragStart[i].y + delta.y);
    }
}

void FieldComponent::updateMarqueeSelection()
{
    if (! marquee)
        return;

    std::vector<int> picked = marqueeBase;
    const auto inside = marquee->expanded (0.0f);
    for (int s = 0; s < activeSources(); ++s)
    {
        const auto pos = position ({ true, s });
        if (inside.contains (toView (pos.x, pos.y)))
            for (const int member : groups.membersOf (s, activeSources()))
                picked.push_back (member);
    }
    setSelection (std::move (picked));
}

bool FieldComponent::canEditVirtualSpeakers() const
{
    return processor.canEditVirtualSpeakers();
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
        mix (getSpread (i));
    }

    const auto currentGroups = processor.getSourceGroups();
    if (! (currentGroups == groups))
    {
        groups = currentGroups;
        notifySelectionChanged();
    }
    for (int i = 0; i < n; ++i)
        mix ((double) groups.groupOf (i));
    for (const int s : selection)
        mix (1000.0 + s);
    if (marquee)
        mix (marquee->getWidth() + marquee->getHeight());

    // Sources that are no longer in use can not stay selected.
    if (! selection.empty() && selection.back() >= n)
        setSelection (selection);

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
    g.drawText ("Front", juce::Rectangle<float> (plane.getCentreX() - 30.0f, plane.getY() - 18.0f, 60.0f, 16.0f),
                juce::Justification::centred);
    g.drawText ("Back", juce::Rectangle<float> (plane.getCentreX() - 30.0f, plane.getBottom() + 2.0f, 60.0f, 16.0f),
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

    // Groups: a soft band around the members of each group (the outline of the convex hull, widened), in the
    // group's colour, with its number above the top member.
    for (int id = 1; id <= tripan::SourceGroups::kMaxGroupId; ++id)
    {
        if (groups.sizeOf (id, n) < 2)
            continue;

        std::vector<tripan::Vec2> pts;
        std::vector<int> members;
        for (int s = 0; s < n; ++s)
            if (groups.groupOf (s) == id)
            {
                const auto pos = position ({ true, s });
                pts.push_back ({ pos.x, pos.y });
                members.push_back (s);
            }

        const auto groupTint = sourceColour (members.front(), groups);
        const auto hull = tripan::convexHull (pts);
        juce::Path path;
        for (size_t i = 0; i < hull.size(); ++i)
        {
            const auto v = toView (hull[i].x, hull[i].y);
            if (i == 0) path.startNewSubPath (v); else path.lineTo (v);
        }
        if (hull.size() >= 3)
        {
            path.closeSubPath();
            g.setColour (groupTint.withAlpha (0.08f));
            g.fillPath (path);
        }
        g.setColour (groupTint.withAlpha (0.2f));
        g.strokePath (path, juce::PathStrokeType (30.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        auto top = toView (hull.front().x, hull.front().y);
        for (const auto& h : hull)
        {
            const auto v = toView (h.x, h.y);
            if (v.y < top.y) top = v;
        }
        g.setColour (groupTint.withAlpha (0.95f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("G" + juce::String (id), juce::Rectangle<float> (30.0f, 12.0f).withCentre ({ top.x, top.y - 27.0f }), juce::Justification::centred);
    }

    // Spread: a semi-transparent circle around each source whose radius is its spread (in metres).
    // It is display only; mouse interaction happens in the inner circle of the source.
    {
        const float pixelsPerMetre = planeBounds().getWidth() / (float) (2.0 * viewHalf);
        for (int s = 0; s < n; ++s)
        {
            const double spread = getSpread (s);
            if (spread <= 1e-3)
                continue;

            const auto pos = position ({ true, s });
            const auto c = toView (pos.x, pos.y);
            const float radius = (float) spread * pixelsPerMetre;
            const auto circle = juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (c);
            g.setColour (sourceColour (s, groups).withAlpha (0.16f));
            g.fillEllipse (circle);
            g.setColour (sourceColour (s, groups).withAlpha (0.6f));
            g.drawEllipse (circle, 1.2f);
        }
    }

    // Links from each source to the speakers it currently feeds, weighted by gain.
    if (showLinks && triangulation != nullptr)
    {
        for (int s = 0; s < n; ++s)
        {
            const auto sp = position ({ true, s });
            const auto gains = tripan::computeGains (*triangulation, { sp.x, sp.y }, getSpread (s));
            const auto from = toView (sp.x, sp.y);
            for (int i = 0; i < gains.count; ++i)
            {
                const auto spk = position ({ false, gains.speaker[(size_t) i] });
                const auto to = toView (spk.x, spk.y);
                const float gain = gains.gain[(size_t) i];
                g.setColour (sourceColour (s, groups).withAlpha (0.15f + 0.75f * gain));
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
        if (const int id = groups.groupOf (s); id > 0) // ring in the colour of the group
        {
            g.setColour (sourceColour (s, groups));
            g.drawEllipse (r.expanded (3.0f), 2.0f);
        }
        if (std::binary_search (selection.begin(), selection.end(), s)) // selected
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawEllipse (r.expanded (7.0f), 1.5f);
        }
        g.setColour (sourceColour (s, groups));
        g.fillEllipse (r);
        g.setColour (hot ? juce::Colours::white : juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (r, hot ? 2.0f : 1.0f);
        g.setColour (juce::Colours::black);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (s + 1), r, juce::Justification::centred);
    }

    // While the spread is being dragged, show its value next to the circle.
    if (draggingSpread && dragging && dragging->isSource)
    {
        const auto pos = position (*dragging);
        const auto c = toView (pos.x, pos.y);
        const float radius = (float) getSpread (dragging->index) * planeBounds().getWidth() / (float) (2.0 * viewHalf);
        const auto label = juce::Rectangle<float> (90.0f, 16.0f).withCentre ({ c.x, c.y + radius + 14.0f });
        g.setColour (juce::Colour (0xff15181d).withAlpha (0.8f));
        g.fillRoundedRectangle (label, 3.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("spread " + juce::String (getSpread (dragging->index), 2) + " m", label, juce::Justification::centred);
    }

    if (marquee)
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (*marquee);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawRect (*marquee, 1.0f);
    }

    g.restoreState();

    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawRoundedRectangle (plane, 6.0f, 1.0f);
}

void FieldComponent::pressAt (juce::Point<float> p, const juce::ModifierKeys& mods)
{
    dragging = findHandleAt (p);
    draggingSpread = false;
    dragSources.clear();
    dragStart.clear();
    marquee.reset();
    groups = processor.getSourceGroups();
    if (isShowing())
        grabKeyboardFocus();

    if (! dragging)
    {
        // Empty space: start a selection rectangle (sources must be editable). Shift extends the selection.
        if (processor.canEditSources() && planeBounds().contains (p))
        {
            marqueeBase = mods.isShiftDown() ? selection : std::vector<int>();
            marqueeOrigin = p;
            marquee = juce::Rectangle<float> (p, p);
            if (! mods.isShiftDown())
                clearSelection();
        }
        return;
    }

    if (dragging->isSource)
    {
        const int s = dragging->index;
        const bool spreadDrag = isSpreadModifier (mods);

        if (! spreadDrag && ! isSingleSourceModifier (mods))
        {
            const auto members = groups.membersOf (s, activeSources());
            if (mods.isShiftDown()) // add or remove the source together with its group
            {
                auto next = selection;
                if (std::binary_search (selection.begin(), selection.end(), s))
                    next.erase (std::remove_if (next.begin(), next.end(), [&] (int v) { return std::find (members.begin(), members.end(), v) != members.end(); }), next.end());
                else
                    next.insert (next.end(), members.begin(), members.end());
                setSelection (std::move (next));

                if (! std::binary_search (selection.begin(), selection.end(), s))
                {
                    dragging.reset(); // removed from the selection: nothing to drag
                    return;
                }
            }
            else if (! std::binary_search (selection.begin(), selection.end(), s))
            {
                setSelection (members);
            }
        }

        if (spreadDrag)
        {
            // The spread keeps its value at the press and then follows the vertical movement of the mouse.
            draggingSpread = true;
            lastSpreadDragY = p.y;
            dragSources = isGroupSpreadModifier (mods) ? movingSet (s) : std::vector<int> { s };
            for (const int member : dragSources)
                srcSpreadP[(size_t) member]->beginChangeGesture();
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            repaint();
            return;
        }

        dragSources = isSingleSourceModifier (mods) ? std::vector<int> { s } : movingSet (s);
        for (const int member : dragSources)
        {
            const auto pos = position ({ true, member });
            dragStart.push_back ({ pos.x, pos.y });
            srcXp[(size_t) member]->beginChangeGesture();
            srcYp[(size_t) member]->beginChangeGesture();
        }
        const auto grabbed = position (*dragging);
        dragGrabStart = { grabbed.x, grabbed.y };
        moveSelectionTo (p);
        repaint();
        return;
    }

    if (! dragging->isVirtual)
    {
        parameter (*dragging, 'x')->beginChangeGesture();
        parameter (*dragging, 'y')->beginChangeGesture();
    }
    moveHandle (*dragging, p);
    repaint();
}

void FieldComponent::dragTo (juce::Point<float> p)
{
    if (marquee)
    {
        marquee = juce::Rectangle<float> (marqueeOrigin, p);
        updateMarqueeSelection();
        repaint();
        return;
    }

    if (! dragging)
        return;

    if (draggingSpread)
    {
        // Up = more spread, down = less; one pixel is the same distance as on the pad (so the top of the
        // circle follows the mouse). Applied step by step and limited at every step, which means there is
        // no "dead zone" after dragging beyond 0 or the maximum. A group changes by the same amount, as far
        // as every member can follow.
        const double metresPerPixel = 2.0 * viewHalf / (double) planeBounds().getWidth();
        const double wanted = (double) (lastSpreadDragY - p.y) * metresPerPixel;
        lastSpreadDragY = p.y;

        std::vector<double> current;
        for (const int member : dragSources)
            current.push_back (getSpread (member));

        const double change = tripan::clampSpreadChange (current, wanted, (double) tripan::params::kMaxSpread);
        for (size_t i = 0; i < dragSources.size(); ++i)
            setSpread (dragSources[i], current[i] + change);
    }
    else if (dragging->isSource)
    {
        moveSelectionTo (p);
    }
    else
    {
        moveHandle (*dragging, p);
    }
}

void FieldComponent::release()
{
    if (marquee)
    {
        marquee.reset();
        marqueeBase.clear();
        repaint();
    }

    if (! dragging)
        return;

    if (draggingSpread)
    {
        for (const int member : dragSources)
            srcSpreadP[(size_t) member]->endChangeGesture();
    }
    else if (dragging->isSource)
    {
        for (const int member : dragSources)
        {
            srcXp[(size_t) member]->endChangeGesture();
            srcYp[(size_t) member]->endChangeGesture();
        }
    }
    else if (! dragging->isVirtual)
    {
        parameter (*dragging, 'x')->endChangeGesture();
        parameter (*dragging, 'y')->endChangeGesture();
    }

    dragging.reset();
    draggingSpread = false;
    dragSources.clear();
    dragStart.clear();
    repaint();
}

void FieldComponent::doubleClickAt (juce::Point<float> p, const juce::ModifierKeys& mods)
{
    // Option + double-click on a source: reset its spread.
    if (isSpreadModifier (mods))
    {
        const auto h = findHandleAt (p);
        if (h && h->isSource)
        {
            // With Shift: everything that moves with the source.
            const auto set = isGroupSpreadModifier (mods) ? movingSet (h->index) : std::vector<int> { h->index };
            for (const int member : set)
            {
                srcSpreadP[(size_t) member]->beginChangeGesture();
                setSpread (member, 0.0);
                srcSpreadP[(size_t) member]->endChangeGesture();
            }
        }
        return;
    }

    // Manual virtual speakers: double-click one to delete it, or empty space to add one there.
    if (! canEditVirtualSpeakers())
        return;

    const auto h = findHandleAt (p);
    if (h && h->isVirtual)
        removeVirtualSpeaker (h->index);
    else if (! h && planeBounds().contains (p))
        addVirtualSpeakerAt (p);
}

void FieldComponent::mouseDown (const juce::MouseEvent& e)       { pressAt (e.position, e.mods); }
void FieldComponent::mouseDrag (const juce::MouseEvent& e)       { dragTo (e.position); }
void FieldComponent::mouseUp (const juce::MouseEvent&)           { release(); }
void FieldComponent::mouseDoubleClick (const juce::MouseEvent& e) { doubleClickAt (e.position, e.mods); }

void FieldComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto h = findHandleAt (e.position);
    const bool changed = h.has_value() != hovered.has_value()
                      || (h && hovered && (h->isSource != hovered->isSource || h->isVirtual != hovered->isVirtual || h->index != hovered->index));
    hovered = h;
    const bool spreading = h && h->isSource && isSpreadModifier (e.mods);
    setMouseCursor (spreading ? juce::MouseCursor::UpDownResizeCursor
                              : h ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (changed)
        repaint();
}
