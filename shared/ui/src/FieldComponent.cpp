// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file FieldComponent.cpp
    @brief Drawing, hit-testing, dragging, spread editing, virtual speaker editing and zoom of the XY pad. */

#include "tlos/FieldComponent.h"
#include "tlos/Parameters.h"

#include "tlos/Groups.h"
#include "tlos/Polar.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace tlos
{

namespace
{
/** The largest 1, 2 or 5 times a power of ten that is not more than `x` (x must be positive). */
double niceStepBelow (double x)
{
    const double base = std::pow (10.0, std::floor (std::log10 (x)));
    const double m = x / base;
    return (m >= 5.0 ? 5.0 : m >= 2.0 ? 2.0 : 1.0) * base;
}

/// View size ratio between two zoom levels.
constexpr double kZoomStep = 1.5;
/// Never zoom in closer than +-2 m (or the whole room, if that is smaller).
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

FieldComponent::FieldComponent (SpatialProcessor& p)
    : processor (p)
{
    auto& apvts = processor.getValueTreeState();
    for (int i = 0; i < tlos::kMaxChannels; ++i)
    {
        srcXp[(size_t) i] = apvts.getParameter (params::sourceId (i, 'x'));
        srcYp[(size_t) i] = apvts.getParameter (params::sourceId (i, 'y'));
        srcSpreadP[(size_t) i] = apvts.getParameter (params::spreadId (i));
        spkXp[(size_t) i] = apvts.getParameter (params::speakerId (i, 'x'));
        spkYp[(size_t) i] = apvts.getParameter (params::speakerId (i, 'y'));
        srcMuteRaw[(size_t) i] = apvts.getRawParameterValue (params::sourceMuteId (i));
        srcSoloRaw[(size_t) i] = apvts.getRawParameterValue (params::sourceSoloId (i));
        spkMuteRaw[(size_t) i] = apvts.getRawParameterValue (params::speakerMuteId (i));
        spkSoloRaw[(size_t) i] = apvts.getRawParameterValue (params::speakerSoloId (i));
    }

    baseTooltip = "Drag a source (its inner circle) to move it with the rest of its group or selection. "
                  "Shift-click adds to the selection; drag on empty space to select several. "
                  "Cmd+G groups the selection, Cmd+Shift+G ungroups it.\n"
                  "Cmd-drag: move one source alone.\n"
                  "Option-drag up/down: spread of the group (+Cmd: of one source). Option-double-click resets it.\n"
                  "Option+Shift-drag up/down: move the group's sources apart / together.\n"
                  "Cmd+Shift-drag up/down: rotate the group counter-clockwise / clockwise.\n"
                  "Right-click a source or speaker: mute, solo, exclude from panning (speakers).";
    setTooltip (baseTooltip);
    setWantsKeyboardFocus (true);

    viewHalf = lastRoomHalf = roomHalf();
    timerCallback();
    startTimerHz (30);
}

FieldComponent::~FieldComponent()
{
    stopTimer();
}

void FieldComponent::setShowOverlay (bool shouldShow)
{
    showOverlay = shouldShow;
    repaint();
}

void FieldComponent::updateOverlay (const IncludedSpeakers&, const std::vector<VirtualSpeaker>&) {}

void FieldComponent::paintOverlay (juce::Graphics&) {}

void FieldComponent::paintBackdrop (juce::Graphics&) {}

namespace
{
/** Tints a region and draws its outline dashed, in the colour of the speaker lines (TriPan's triangulation, VecPan's pairs). */
void paintRegion (juce::Graphics& g, const juce::Path& region)
{
    const auto blue = juce::Colour (0xff7aa2c8);
    g.setColour (blue.withAlpha (0.06f));
    g.fillPath (region);

    juce::Path dashed;
    const float dashes[] { 5.0f, 4.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, region, dashes, 2);
    g.setColour (blue.withAlpha (0.65f));
    g.fillPath (dashed);
}
}

void FieldComponent::paintFalloffRegion (juce::Graphics& g, const Hull& hull) const
{
    if (! hull.hasArea())
        return;

    juce::Path region;
    bool first = true;
    for (const auto& corner : hull.getCorners())
    {
        const auto p = toView (corner.x, corner.y);
        if (first)
            region.startNewSubPath (p);
        else
            region.lineTo (p);
        first = false;
    }
    region.closeSubPath();
    paintRegion (g, region);
}

void FieldComponent::paintFalloffCircle (juce::Graphics& g, double radius) const
{
    if (! (radius > 0.0))
        return;

    const auto centre = toView (0.0, 0.0);
    const float r = std::abs (toView (radius, 0.0).x - centre.x);
    juce::Path region;
    region.addEllipse (centre.x - r, centre.y - r, 2.0f * r, 2.0f * r);
    paintRegion (g, region);
}

SourceGains FieldComponent::getLinkGains (int, Vec2, double) const { return {}; }

void FieldComponent::setShowLevels (bool shouldShow)
{
    showLevels = shouldShow;
    sourceLevels.fill (0.0f);
    speakerLevels.fill (0.0f);
    speakerHold.fill (0.0f);
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

std::optional<FieldComponent::Handle> FieldComponent::findHandleAt (juce::Point<float> p, bool ignoreEditModes) const
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
    if (ignoreEditModes || processor.canEditSpeakers()) consider (false, activeSpeakers());
    if (! ignoreEditModes && canEditVirtualSpeakers())  consider (false, (int) virtualSpeakers.size(), true);
    if (ignoreEditModes || processor.canEditSources())  consider (true, activeSources());
    return best;
}

FieldComponent::DragMode FieldComponent::dragModeFor (const juce::ModifierKeys& mods) noexcept
{
    const bool alt = mods.isAltDown(), cmd = mods.isCommandDown(), shift = mods.isShiftDown();

    if (alt && cmd)   return DragMode::SpreadOne;
    if (alt && shift) return DragMode::Scale;
    if (cmd && shift) return DragMode::Rotate;
    if (alt)          return DragMode::SpreadGroup;
    if (cmd)          return DragMode::MoveOne;
    return DragMode::Move;
}

bool FieldComponent::isSpreadModifier (const juce::ModifierKeys& mods) noexcept
{
    const auto mode = dragModeFor (mods);
    return mode == DragMode::SpreadGroup || mode == DragMode::SpreadOne;
}

double FieldComponent::getSpread (int source) const
{
    if (source < 0 || source >= tlos::kMaxChannels || ! processor.usesSpread())
        return 0.0;

    auto* prm = srcSpreadP[(size_t) source];
    return (double) prm->convertFrom0to1 (prm->getValue());
}

void FieldComponent::setSpread (int source, double metres)
{
    if (source < 0 || source >= tlos::kMaxChannels || ! processor.canEditSources())
        return;

    auto* prm = srcSpreadP[(size_t) source];
    prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (0.0f, params::kMaxSpread, (float) metres)));
}

juce::Colour FieldComponent::sourceColour (int source, const tlos::SourceGroups& groups)
{
    const int id = groups.groupOf (source);
    if (id > 0)
        for (int i = 0; i < tlos::kMaxChannels; ++i)
            if (groups.groupOf (i) == id)
                return individualColour (i); // the lowest member

    return individualColour (source);
}

bool FieldComponent::isSingleSourceModifier (const juce::ModifierKeys& mods) noexcept
{
    return dragModeFor (mods) == DragMode::MoveOne;
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

juce::String FieldComponent::nameOf (const Handle& h) const
{
    return (h.isSource ? juce::String ("Source ") : h.isVirtual ? juce::String ("Virtual speaker ") : processor.getOutputNoun() + " ") + juce::String (h.index + 1);
}

juce::String FieldComponent::positionText (const Handle& h) const
{
    const auto p = position (h);

    if (processor.getCoordinateMode() == SpatialProcessor::CoordinateMode::Polar)
    {
        const auto polar = toPolar ({ p.x, p.y });
        return "azimuth " + juce::String (std::abs (polar.azimuthDegrees) < 0.05 ? 0.0 : polar.azimuthDegrees, 1)
             + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) + ", distance " + juce::String (polar.distance, 2) + " m";
    }

    const auto clean = [] (double v) { return std::abs (v) < 0.005 ? 0.0 : v; };
    return "x " + juce::String (clean (p.x), 2) + " m, y " + juce::String (clean (p.y), 2) + " m";
}

juce::String FieldComponent::getTooltip()
{
    if (hovered)
    {
        auto text = nameOf (*hovered) + ": " + positionText (*hovered) + "\n";

        if (hovered->isSource)
        {
            const int id = groups.groupOf (hovered->index);
            if (id > 0)
                text += "Group " + juce::String (id) + " (" + juce::String (groups.sizeOf (id, activeSources())) + " sources)\n";
        }

        return text + baseTooltip;
    }

    return baseTooltip;
}

void FieldComponent::writeDragPositions (const std::vector<Vec2>& positions)
{
    const auto set = [this] (juce::RangedAudioParameter* prm, double v)
    {
        const float clamped = juce::jlimit (-(float) roomHalf(), (float) roomHalf(), (float) v);
        prm->setValueNotifyingHost (prm->convertTo0to1 (clamped));
    };

    for (size_t i = 0; i < dragSources.size() && i < positions.size(); ++i)
    {
        set (srcXp[(size_t) dragSources[i]], positions[i].x);
        set (srcYp[(size_t) dragSources[i]], positions[i].y);
    }
}

void FieldComponent::moveSelectionTo (juce::Point<float> p)
{
    if (dragSources.empty() || ! processor.canEditSources())
        return;

    // The grabbed source goes to the pointer and the others keep their offsets to it; the movement is
    // limited as a whole, so that the shape never changes when a member would leave the pad.
    const auto pos = fromView (p);
    const auto delta = tlos::clampTranslation (dragStart, { pos.x - dragGrabStart.x, pos.y - dragGrabStart.y },
                                                 roomHalf());

    auto moved = dragStart;
    for (auto& q : moved)
    {
        q.x += delta.x;
        q.y += delta.y;
    }
    writeDragPositions (moved);
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
    if (! canEditVirtualSpeakers() || (int) list.size() >= SpatialProcessor::kMaxVirtualSpeakers)
        return false;

    const auto pos = fromView (p);
    const float limit = (float) roomHalf();
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
        const float limit = (float) roomHalf();
        list[(size_t) h.index] = { (double) juce::jlimit (-limit, limit, (float) pos.x), (double) juce::jlimit (-limit, limit, (float) pos.y) };
        processor.setManualVirtualSpeakers (list, false);   // a drag has its own change gesture (pressAt() .. release())
        refresh();
        return;
    }

    if (h.isSource ? ! processor.canEditSources() : ! processor.canEditSpeakers())
        return;

    const auto pos = fromView (p);
    const auto set = [this] (juce::RangedAudioParameter* prm, double v)
    {
        const float clamped = juce::jlimit (-(float) roomHalf(), (float) roomHalf(), (float) v);
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
    v.half = std::clamp (std::max (maxX - minX, maxY - minY) * 0.5 * 1.1 + 0.5, std::min (kMinHalf, roomHalf()), roomHalf());
    return v;
}

bool FieldComponent::canZoomIn() const
{
    const auto fit = fittedView();
    return fit && fit->half < viewHalf - 1e-6;
}

bool FieldComponent::canZoomOut() const
{
    return viewHalf < roomHalf() - 1e-6;
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

    const double fullHalf = roomHalf();
    const double newHalf = std::min (fullHalf, viewHalf * kZoomStep);
    const double fraction = (newHalf - viewHalf) / (fullHalf - viewHalf); // how far to the full view
    viewCentre = viewCentre * (1.0 - fraction);
    viewHalf = newHalf;
    if (viewHalf >= fullHalf - 1e-9)
        viewCentre = {};
    repaint();
}

void FieldComponent::timerCallback()
{
    const int n = activeSources(), m = activeSpeakers();

    size_t hash = std::hash<int> {} (n * 1000 + m);
    const auto mix = [&] (double v) { hash = hash * 1000003u ^ std::hash<double> {} (v); };

    // A new room size shows the whole room again.
    if (! juce::approximatelyEqual (roomHalf(), lastRoomHalf))
    {
        lastRoomHalf = roomHalf();
        viewHalf = lastRoomHalf;
        viewCentre = {};
    }
    mix (lastRoomHalf);
    mix (processor.getCoordinateMode() == SpatialProcessor::CoordinateMode::Polar ? 1.0 : 0.0);   // the grid follows the mode

    excludedSpeakers = processor.getExcludedSpeakers();

    for (int i = 0; i < m; ++i)
    {
        const auto pos = position ({ false, i });
        mix (pos.x);
        mix (pos.y);
        mix (excludedSpeakers[(size_t) i] ? 1.0 : 0.0);
        mix (spkMuteRaw[(size_t) i]->load() + 2.0 * spkSoloRaw[(size_t) i]->load());
    }
    for (int i = 0; i < n; ++i)
    {
        const auto pos = position ({ true, i });
        mix (pos.x);
        mix (pos.y);
        mix (getSpread (i));
        mix (srcMuteRaw[(size_t) i]->load() + 2.0 * srcSoloRaw[(size_t) i]->load());
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

    updateOverlay (processor.getIncludedSpeakers(), virtualSpeakers);

    if (showLevels)
    {
        // Peak levels on a -60..0 dB scale: they rise at once and fall by 0.05 per tick (about 1.2 s for the whole scale at 30 Hz);
        // a speaker's hold mark stays for 1 s and then falls with the level.
        const auto toScale = [] (float peak) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -60.0f) + 60.0f) / 60.0f); };
        for (int s = 0; s < kMaxChannels; ++s)
        {
            const float peak = processor.takeInputPeak (s);
            sourceLevels[(size_t) s] = s < n ? std::max (toScale (peak), sourceLevels[(size_t) s] - 0.05f) : 0.0f;
        }
        for (int i = 0; i < kMaxChannels; ++i)
        {
            const float level = toScale (processor.takeOutputPeak (i));
            auto& shown = speakerLevels[(size_t) i];
            shown = i < m ? std::max (level, shown - 0.05f) : 0.0f;
            if (shown >= speakerHold[(size_t) i])
            {
                speakerHold[(size_t) i] = shown;
                speakerHoldTicks[(size_t) i] = 30;
            }
            else if (speakerHoldTicks[(size_t) i] > 0)
                --speakerHoldTicks[(size_t) i];
            else
                speakerHold[(size_t) i] = std::max (shown, speakerHold[(size_t) i] - 0.05f);
        }
        for (int i = 0; i < kMaxChannels; ++i)
        {
            mix (std::round (64.0 * sourceLevels[(size_t) i]));
            mix (std::round (64.0 * speakerLevels[(size_t) i]));
            mix (std::round (64.0 * speakerHold[(size_t) i]));
        }
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
    const double minorStep = niceStepBelow (viewHalf / 6.0);   // 1 m for the default room, 10 m for a 100 m room
    const double majorStep = 5.0 * minorStep;

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
    // Polar mode: rings of equal distance and spokes of equal azimuth around the listener instead of the x / y lines.
    const auto drawPolarGrid = [&]
    {
        const auto origin = toView (0.0, 0.0);
        const double scale = (double) plane.getWidth() / (2.0 * viewHalf);              // pixels per metre
        const double reach = std::hypot (std::abs (viewCentre.x) + viewHalf, std::abs (viewCentre.y) + viewHalf);   // farthest visible distance

        // Rings, labelled in metres.
        for (long n = 1; (double) n * minorStep <= reach + 1e-9; ++n)
        {
            const double r = (double) n * minorStep;
            const bool major = std::abs (std::remainder (r, majorStep)) < 1e-9;
            g.setColour (juce::Colours::white.withAlpha (major ? 0.16f : 0.06f));
            const float radius = (float) (r * scale);
            g.drawEllipse (origin.x - radius, origin.y - radius, 2.0f * radius, 2.0f * radius, 1.0f);

            // Labelled on the 45 degree diagonal to the front right: speakers and sources often sit on the axes.
            const auto label = toView (r * 0.70710678, r * 0.70710678);
            if (major && plane.contains (label) && label.x < plane.getRight() - 50.0f && label.y > plane.getY() + 16.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (formatMetres (r), juce::Rectangle<float> (label.x + 3.0f, label.y - 14.0f, 50.0f, 13.0f), juce::Justification::left);
            }
        }

        // Spokes every 15 degrees, stronger every 30, strongest on the axes. Azimuth is counter-clockwise from the front.
        for (int deg = 0; deg < 360; deg += 15)
        {
            const bool axis = deg % 90 == 0, major = deg % 30 == 0;
            const double a = deg * juce::MathConstants<double>::pi / 180.0;
            const double dx = -std::sin (a), dy = std::cos (a);   // plane direction (x right, y front)
            const auto end = toView (dx * reach, dy * reach);
            g.setColour (juce::Colours::white.withAlpha (axis ? 0.35f : major ? 0.16f : 0.06f));
            g.drawLine (origin.x, origin.y, end.x, end.y, axis ? 1.5f : 1.0f);

            // Label where the spoke leaves the visible plane (slab method on the view rectangle).
            if (major)
            {
                const double vx = dx, vy = -dy;   // view direction (y grows downwards)
                double tMax = 1e9;
                if (std::abs (vx) > 1e-9)
                    tMax = std::min (tMax, std::max (((double) plane.getX() - origin.x) / vx, ((double) plane.getRight() - origin.x) / vx));
                if (std::abs (vy) > 1e-9)
                    tMax = std::min (tMax, std::max (((double) plane.getY() - origin.y) / vy, ((double) plane.getBottom() - origin.y) / vy));

                if (tMax > 0.0 && tMax < 1e8)
                {
                    const double t = tMax - 16.0;
                    const juce::Point<float> at ((float) (origin.x + vx * t), (float) (origin.y + vy * t));
                    if (plane.contains (at))
                    {
                        const int shown = (int) wrapAzimuth ((double) deg);
                        g.setColour (juce::Colours::white.withAlpha (0.45f));
                        g.setFont (juce::FontOptions (11.0f));
                        g.drawText (juce::String (shown) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")),
                                    juce::Rectangle<float> (34.0f, 13.0f).withCentre (at), juce::Justification::centred);
                    }
                }
            }
        }
    };

    if (processor.getCoordinateMode() == SpatialProcessor::CoordinateMode::Polar)
    {
        drawPolarGrid();
    }
    else
    {
        drawLines (true);
        drawLines (false);
    }

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

    // What the plugin draws under everything else (TriPan: the triangulation).
    paintBackdrop (g);
    if (showOverlay)
        paintOverlay (g);

    // Groups: a soft band around the members of each group (the outline of the convex hull, widened), in the
    // group's colour, with its number above the top member.
    for (int id = 1; id <= tlos::SourceGroups::kMaxGroupId; ++id)
    {
        if (groups.sizeOf (id, n) < 2)
            continue;

        std::vector<Vec2> pts;
        std::vector<int> members;
        for (int s = 0; s < n; ++s)
            if (groups.groupOf (s) == id)
            {
                const auto pos = position ({ true, s });
                pts.push_back ({ pos.x, pos.y });
                members.push_back (s);
            }

        const auto groupTint = sourceColour (members.front(), groups);
        const auto hull = tlos::convexHull (pts);
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

    // What trim, mute and solo make of every source and speaker; a silenced one is drawn dimmed.
    ChannelGains channelGains;
    processor.getChannelGains (channelGains);

    // Links from each source to the speakers it currently feeds, weighted by gain (and by trim, mute and solo).
    if (showLinks)
    {
        for (int s = 0; s < n; ++s)
        {
            const auto sp = position ({ true, s });
            const auto gains = getLinkGains (s, { sp.x, sp.y }, getSpread (s));
            const auto from = toView (sp.x, sp.y);
            for (int i = 0; i < gains.count; ++i)
            {
                const int channel = gains.speaker[(size_t) i];
                const float gain = std::min (gains.gain[(size_t) i] * channelGains.sources[(size_t) s] * channelGains.speakers[(size_t) channel], 1.0f);
                if (gain < 0.002f)
                    continue;

                const auto spk = position ({ false, channel });
                const auto to = toView (spk.x, spk.y);
                g.setColour (sourceColour (s, groups).withAlpha (0.15f + 0.75f * gain));
                g.drawLine (from.x, from.y, to.x, to.y, 0.8f + 2.4f * gain);
            }
        }
    }

    // Level rings: an arc from the top, clockwise, as long as the level (-60..0 dB): green, amber above -12 dB, red above -3 dB.
    const auto levelColour = [] (float level)
    {
        return level > 0.95f ? juce::Colour (0xffff5a5a) : level > 0.8f ? juce::Colour (0xffe6b450) : juce::Colour (0xff5fd38d);
    };
    const auto drawRing = [&] (juce::Point<float> c, float radius, float level, float hold, float thickness)
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (c), thickness);
        if (level > 0.004f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, 0.0f, level * juce::MathConstants<float>::twoPi, true);
            g.setColour (levelColour (level).withAlpha (0.95f));
            g.strokePath (arc, juce::PathStrokeType (thickness));
        }
        if (hold > 0.02f)
        {
            const float angle = hold * juce::MathConstants<float>::twoPi;
            const auto tip = c.getPointOnCircumference (radius, angle);
            g.setColour (levelColour (hold));
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (tip));
        }
    };
    if (showLevels)
    {
        for (int i = 0; i < m; ++i)
        {
            const auto pos = position ({ false, i });
            drawRing (toView (pos.x, pos.y), 12.0f, speakerLevels[(size_t) i], speakerHold[(size_t) i], 3.0f);
        }
        for (int s = 0; s < n; ++s)
        {
            const auto pos = position ({ true, s });
            drawRing (toView (pos.x, pos.y), 15.0f, sourceLevels[(size_t) s], 0.0f, 2.0f);
        }
    }

    // Speakers
    for (int i = 0; i < m; ++i)
    {
        const auto pos = position ({ false, i });
        const auto c = toView (pos.x, pos.y);
        const bool hot = (hovered && ! hovered->isSource && hovered->index == i) || (dragging && ! dragging->isSource && dragging->index == i);
        const auto r = juce::Rectangle<float> (14.0f, 14.0f).withCentre (c);
        const bool excluded = excludedSpeakers[(size_t) i];
        const bool muted = spkMuteRaw[(size_t) i]->load() >= 0.5f, soloed = spkSoloRaw[(size_t) i]->load() >= 0.5f;
        const float alpha = ! excluded && channelGains.speakers[(size_t) i] <= 0.0f ? 0.35f : 1.0f; // silenced: dimmed

        if (excluded) // not part of the panning: hollow and grey, struck through
        {
            g.setColour (juce::Colour (0xff8a93a0).withAlpha (hot ? 1.0f : 0.8f));
            g.drawRoundedRectangle (r, 2.0f, 1.5f);
            g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.2f);
        }
        else
        {
            g.setColour ((hot ? juce::Colours::white : juce::Colour (0xffdfe6ee)).withAlpha (alpha));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (juce::Colour (0xff15181d));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (i + 1), r, juce::Justification::centred);
        }

        if (soloed || muted) // badge: S (solo) or M (mute) at the upper right
        {
            g.setColour (soloed && ! muted ? juce::Colour (0xffe6b450) : juce::Colour (0xffff7a7a));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (muted ? "M" : "S", juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ r.getRight() + 4.0f, r.getY() - 3.0f }), juce::Justification::centred);
        }
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
        const bool muted = srcMuteRaw[(size_t) s]->load() >= 0.5f, soloed = srcSoloRaw[(size_t) s]->load() >= 0.5f;
        const float alpha = channelGains.sources[(size_t) s] <= 0.0f ? 0.35f : 1.0f; // silenced: dimmed
        g.setColour (sourceColour (s, groups).withAlpha (alpha));
        g.fillEllipse (r);
        g.setColour ((hot ? juce::Colours::white : juce::Colours::black.withAlpha (0.6f)).withMultipliedAlpha (alpha));
        g.drawEllipse (r, hot ? 2.0f : 1.0f);
        g.setColour (juce::Colours::black.withAlpha (alpha));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (s + 1), r, juce::Justification::centred);

        if (soloed || muted) // badge: S (solo) or M (mute) at the upper right
        {
            g.setColour (soloed && ! muted ? juce::Colour (0xffe6b450) : juce::Colour (0xffff7a7a));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (muted ? "M" : "S", juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ r.getRight() + 3.0f, r.getY() - 2.0f }), juce::Justification::centred);
        }
    }

    // While a vertical drag is going on, show its value: next to the circle for spread, at the centre of gravity
    // of the group for scale and rotation.
    if (dragging && dragging->isSource && dragMode != DragMode::Move && dragMode != DragMode::MoveOne)
    {
        juce::String text;
        juce::Point<float> c;

        if (draggingSpread)
        {
            const auto pos = position (*dragging);
            c = toView (pos.x, pos.y);
            c.y += (float) getSpread (dragging->index) * planeBounds().getWidth() / (float) (2.0 * viewHalf) + 14.0f;
            text = "spread " + juce::String (getSpread (dragging->index), 2) + " m";
        }
        else
        {
            c = toView (dragCentre.x, dragCentre.y);
            c.y += 16.0f;
            text = dragMode == DragMode::Scale ? "scale " + juce::String (juce::roundToInt (dragScale * 100.0)) + " %"
                                               : "rotate " + juce::String (dragAngle * 180.0 / juce::MathConstants<double>::pi, 1) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ c.x, c.y - 16.0f }));
        }

        const auto label = juce::Rectangle<float> (90.0f, 16.0f).withCentre (c);
        g.setColour (juce::Colour (0xff15181d).withAlpha (0.8f));
        g.fillRoundedRectangle (label, 3.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (text, label, juce::Justification::centred);
    }

    // While a source, speaker or virtual speaker is being moved, show its position next to it.
    if (dragging && ! draggingSpread && (dragMode == DragMode::Move || dragMode == DragMode::MoveOne))
    {
        const auto pos = position (*dragging);
        const auto c = toView (pos.x, pos.y) + juce::Point<float> (0.0f, 26.0f);
        const auto label = juce::Rectangle<float> (200.0f, 16.0f).withCentre (c);
        g.setColour (juce::Colour (0xff15181d).withAlpha (0.8f));
        g.fillRoundedRectangle (label, 3.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (positionText (*dragging), label, juce::Justification::centred);
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
        dragMode = dragModeFor (mods);
        draggingSpread = dragMode == DragMode::SpreadGroup || dragMode == DragMode::SpreadOne;

        if (draggingSpread && ! processor.usesSpread())   // the plugin has no use for the spread: nothing to drag
        {
            draggingSpread = false;
            dragging.reset();
            return;
        }

        if (dragMode == DragMode::Move)
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

        if (draggingSpread)
        {
            // The spread keeps its value at the press and then follows the vertical movement of the mouse.
            lastSpreadDragY = p.y;
            dragSources = dragMode == DragMode::SpreadOne ? std::vector<int> { s } : movingSet (s);
            for (const int member : dragSources)
                srcSpreadP[(size_t) member]->beginChangeGesture();
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            repaint();
            return;
        }

        dragSources = dragMode == DragMode::MoveOne ? std::vector<int> { s } : movingSet (s);

        if ((dragMode == DragMode::Scale || dragMode == DragMode::Rotate) && dragSources.size() < 2)
        {
            // Nothing to scale or rotate: a single source has no shape.
            dragging.reset();
            dragSources.clear();
            dragMode = DragMode::Move;
            return;
        }

        for (const int member : dragSources)
        {
            const auto pos = position ({ true, member });
            dragStart.push_back ({ pos.x, pos.y });
            srcXp[(size_t) member]->beginChangeGesture();
            srcYp[(size_t) member]->beginChangeGesture();
        }

        if (dragMode == DragMode::Scale || dragMode == DragMode::Rotate)
        {
            dragCentre = tlos::centroid (dragStart);
            dragScale = 1.0;
            dragAngle = 0.0;
            lastSpreadDragY = p.y;
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            repaint();
            return;
        }

        const auto grabbed = position (*dragging);
        dragGrabStart = { grabbed.x, grabbed.y };
        moveSelectionTo (p);
        repaint();
        return;
    }

    if (dragging->isVirtual)
    {
        processor.beginVirtualSpeakerGesture (dragging->index);
    }
    else
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

        const double change = tlos::clampSpreadChange (current, wanted, (double) params::kMaxSpread);
        for (size_t i = 0; i < dragSources.size(); ++i)
            setSpread (dragSources[i], current[i] + change);
    }
    else if (dragging->isSource && (dragMode == DragMode::Scale || dragMode == DragMode::Rotate))
    {
        // Vertical drags, relative to the press and limited at every step (no dead zone at the limits).
        const double up = (double) (lastSpreadDragY - p.y);
        lastSpreadDragY = p.y;
        const double limit = roomHalf();

        auto result = dragStart;
        if (dragMode == DragMode::Scale)
        {
            const double highest = std::max (kMinScale, tlos::maxScaleFactor (dragStart, dragCentre, limit));
            dragScale = std::clamp (dragScale * std::exp (kScalePerPixel * up), kMinScale, highest);
            for (auto& q : result)
            {
                q.x = dragCentre.x + dragScale * (q.x - dragCentre.x);
                q.y = dragCentre.y + dragScale * (q.y - dragCentre.y);
            }
        }
        else
        {
            dragAngle = tlos::clampRotation (dragStart, dragCentre, dragAngle, dragAngle + up * kRotationPerPixel, limit);
            for (auto& q : result)
                q = tlos::rotateAbout (q, dragCentre, dragAngle);
        }

        if (processor.canEditSources())
            writeDragPositions (result);
        repaint();
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
    else if (dragging->isVirtual)
    {
        processor.endVirtualSpeakerGesture (dragging->index);
    }
    else
    {
        parameter (*dragging, 'x')->endChangeGesture();
        parameter (*dragging, 'y')->endChangeGesture();
    }

    dragging.reset();
    draggingSpread = false;
    dragMode = DragMode::Move;
    dragSources.clear();
    dragStart.clear();
    repaint();
}

void FieldComponent::doubleClickAt (juce::Point<float> p, const juce::ModifierKeys& mods)
{
    // Option + double-click on a source: reset its spread.
    if (isSpreadModifier (mods) && processor.usesSpread())
    {
        const auto h = findHandleAt (p);
        if (h && h->isSource)
        {
            // Option: everything that moves with the source; Option+Cmd: the source alone.
            const auto set = dragModeFor (mods) == DragMode::SpreadOne ? std::vector<int> { h->index } : movingSet (h->index);
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

void FieldComponent::toggleSwitch (const juce::String& parameterId)
{
    if (auto* parameter = processor.getValueTreeState().getParameter (parameterId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getValue() >= 0.5f ? 0.0f : 1.0f);
        parameter->endChangeGesture();
    }
}

void FieldComponent::toggleMute (const Handle& h)
{
    if (h.isVirtual || h.index < 0 || h.index >= tlos::kMaxChannels)
        return;

    toggleSwitch (h.isSource ? params::sourceMuteId (h.index) : params::speakerMuteId (h.index));
    refresh();
}

void FieldComponent::toggleSolo (const Handle& h)
{
    if (h.isVirtual || h.index < 0 || h.index >= tlos::kMaxChannels)
        return;

    toggleSwitch (h.isSource ? params::sourceSoloId (h.index) : params::speakerSoloId (h.index));
    refresh();
}

void FieldComponent::toggleExclude (int speaker)
{
    processor.setSpeakerExcluded (speaker, ! processor.isSpeakerExcluded (speaker));
    refresh();
}

void FieldComponent::showHandleMenu (const Handle& h)
{
    if (h.isVirtual)
        return;

    const bool muted = (h.isSource ? srcMuteRaw : spkMuteRaw)[(size_t) h.index]->load() >= 0.5f;
    const bool soloed = (h.isSource ? srcSoloRaw : spkSoloRaw)[(size_t) h.index]->load() >= 0.5f;

    juce::PopupMenu menu;
    menu.addSectionHeader ((h.isSource ? juce::String ("Source ") : processor.getOutputNoun() + " ") + juce::String (h.index + 1));
    menu.addItem (1, "Mute", true, muted);
    menu.addItem (2, "Solo", true, soloed);
    if (! h.isSource)
        menu.addItem (3, "Exclude from panning", true, excludedSpeakers[(size_t) h.index]);

    menu.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<FieldComponent> (this), h] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == 1) safe->toggleMute (h);
        if (result == 2) safe->toggleSolo (h);
        if (result == 3) safe->toggleExclude (h.index);
    });
}

void FieldComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) // right-click: the menu of the source or speaker under the mouse, never a drag
    {
        if (const auto h = findHandleAt (e.position, true))
            showHandleMenu (*h);
        return;
    }

    pressAt (e.position, e.mods);
}
void FieldComponent::mouseDrag (const juce::MouseEvent& e)       { dragTo (e.position); }
void FieldComponent::mouseUp (const juce::MouseEvent&)           { release(); }
void FieldComponent::mouseDoubleClick (const juce::MouseEvent& e) { doubleClickAt (e.position, e.mods); }

void FieldComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto h = findHandleAt (e.position);
    const bool changed = h.has_value() != hovered.has_value()
                      || (h && hovered && (h->isSource != hovered->isSource || h->isVirtual != hovered->isVirtual || h->index != hovered->index));
    hovered = h;
    const auto mode = dragModeFor (e.mods);
    const bool spreading = h && h->isSource && mode != DragMode::Move && mode != DragMode::MoveOne; // vertical drags
    setMouseCursor (spreading ? juce::MouseCursor::UpDownResizeCursor
                              : h ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (changed)
        repaint();
}
}
