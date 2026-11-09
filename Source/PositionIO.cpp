// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PositionIO.h"
#include "Parameters.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace tripan::io
{
namespace
{
constexpr int kFormatVersion = 1;

const char* rootTag (Kind k)  { return k == Kind::Sources ? "TriPanSources" : "TriPanSpeakers"; }
const char* entryTag (Kind k) { return k == Kind::Sources ? "Source" : "Speaker"; }

juce::String idFor (Kind k, int index, char axis)
{
    return k == Kind::Sources ? params::sourceId (index, axis) : params::speakerId (index, axis);
}

float readParam (const TriPanAudioProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

void writeParam (TriPanAudioProcessor& p, const juce::String& id, float value)
{
    if (auto* prm = p.getValueTreeState().getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

/** Coordinates are written rounded to 3 decimal places (1 mm); a tiny negative value becomes 0.000.
    The text is padded on the left to a fixed width (the width of "-10.000"), so that positive and
    negative numbers line up vertically in the file. Import ignores the padding. */
juce::String formatCoordinate (float value)
{
    auto text = juce::String (value, 3);
    if (text == "-0.000")
        text = "0.000";
    return text.paddedLeft (' ', 7);
}

/** Reads a numeric attribute. Returns false if it is missing or is not a number (JUCE's own conversion
    would turn text such as "abc" into 0). */
bool readNumber (const juce::XmlElement& e, const char* name, double& out)
{
    if (! e.hasAttribute (name))
        return false;

    const auto text = e.getStringAttribute (name).trim();
    if (text.isEmpty() || ! text.containsAnyOf ("0123456789") || ! text.containsOnly ("+-.0123456789eE"))
        return false;

    out = text.getDoubleValue();
    return std::isfinite (out);
}

struct Entry
{
    float x, y;
    float spread = -1.0f; // negative: the file has none
};
}

juce::String describe (Kind k)
{
    return k == Kind::Sources ? "source" : "speaker";
}

std::unique_ptr<juce::XmlElement> toXml (const TriPanAudioProcessor& p, Kind kind)
{
    const int count = kind == Kind::Sources ? p.getActiveSources() : p.getActiveSpeakers();

    auto root = std::make_unique<juce::XmlElement> (rootTag (kind));
    root->setAttribute ("version", kFormatVersion);
    root->setAttribute ("count", count);

    if (kind == Kind::Speakers)
    {
        switch (p.getVirtualMode())
        {
            case VirtualMode::Auto:   root->setAttribute ("virtual", "auto");   break;
            case VirtualMode::Manual: root->setAttribute ("virtual", "manual"); break;
            case VirtualMode::Off:    root->setAttribute ("virtual", "off");    break;
        }
    }

    for (int i = 0; i < count; ++i)
    {
        auto* e = root->createNewChildElement (entryTag (kind));
        e->setAttribute ("index", juce::String (i + 1).paddedLeft (' ', 2)); // padded like the coordinates
        e->setAttribute ("x", formatCoordinate (readParam (p, idFor (kind, i, 'x'))));
        e->setAttribute ("y", formatCoordinate (readParam (p, idFor (kind, i, 'y'))));
        if (kind == Kind::Sources)
            e->setAttribute ("spread", formatCoordinate (readParam (p, params::spreadId (i))));
    }

    // The user's own virtual speakers belong to the layout. (Automatic ones are recomputed on import.)
    if (kind == Kind::Speakers && p.getVirtualMode() == VirtualMode::Manual)
    {
        int index = 1;
        for (const auto& v : p.getManualVirtualSpeakers())
        {
            auto* e = root->createNewChildElement ("VirtualSpeaker");
            e->setAttribute ("index", juce::String (index++).paddedLeft (' ', 2));
            e->setAttribute ("x", formatCoordinate ((float) v.x));
            e->setAttribute ("y", formatCoordinate ((float) v.y));
        }
    }

    return root;
}

juce::Result save (const TriPanAudioProcessor& p, Kind kind, const juce::File& file)
{
    const auto xml = toXml (p, kind);

    // A long wrap length keeps every entry on one line, so the columns stay aligned.
    juce::XmlElement::TextFormat format;
    format.lineWrapLength = 200;

    if (! file.replaceWithText (xml->toString (format)))
        return juce::Result::fail ("Could not write to " + file.getFullPathName());

    return juce::Result::ok();
}

juce::Result load (TriPanAudioProcessor& p, Kind kind, const juce::File& file, ImportReport& report)
{
    report = {};

    if (! file.existsAsFile())
        return juce::Result::fail ("The file does not exist.");

    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return juce::Result::fail ("The file is not valid XML.");

    if (! xml->hasTagName (rootTag (kind)))
    {
        const auto other = kind == Kind::Sources ? Kind::Speakers : Kind::Sources;
        if (xml->hasTagName (rootTag (other)))
            return juce::Result::fail ("This file contains " + describe (other) + " positions, not " + describe (kind) + " positions.");
        return juce::Result::fail ("This is not a TriPan " + describe (kind) + " file.");
    }

    if (xml->getIntAttribute ("version", 1) > kFormatVersion)
        return juce::Result::fail ("The file was written by a newer version of TriPan.");

    // Validate everything first, so a bad file changes nothing. Later entries win on duplicate indices.
    std::map<int, Entry> entries;
    int ordinal = 0;
    const float range = params::kCoordRange;

    for (auto* e : xml->getChildWithTagNameIterator (entryTag (kind)))
    {
        ++ordinal;
        const int index = e->getIntAttribute ("index", ordinal);
        double x = 0.0, y = 0.0;

        if (index < 1 || index > kMaxChannels || ! readNumber (*e, "x", x) || ! readNumber (*e, "y", y))
        {
            ++report.skipped;
            continue;
        }

        const float cx = juce::jlimit (-range, range, (float) x);
        const float cy = juce::jlimit (-range, range, (float) y);
        if (! juce::approximatelyEqual (cx, (float) x) || ! juce::approximatelyEqual (cy, (float) y))
            ++report.clamped;

        Entry entry { cx, cy };
        double spread = 0.0;
        if (kind == Kind::Sources && readNumber (*e, "spread", spread)) // not a number: ignored
        {
            entry.spread = juce::jlimit (0.0f, params::kMaxSpread, (float) spread);
            if (! juce::approximatelyEqual (entry.spread, (float) spread))
                ++report.clamped;
        }

        entries[index] = entry;
    }

    if (entries.empty())
        return juce::Result::fail ("The file contains no valid " + describe (kind) + " positions.");

    // Speaker files may also say how the virtual speakers are set. Validated before anything is changed.
    std::optional<VirtualMode> newVirtualMode;
    std::vector<Vec2> newManual;

    if (kind == Kind::Speakers && xml->hasAttribute ("virtual"))
    {
        const auto setting = xml->getStringAttribute ("virtual").trim().toLowerCase();

        if (setting == "auto")        newVirtualMode = VirtualMode::Auto;
        else if (setting == "off")    newVirtualMode = VirtualMode::Off;
        else if (setting == "manual") newVirtualMode = VirtualMode::Manual;
        else return juce::Result::fail ("Unknown virtual speaker setting \"" + xml->getStringAttribute ("virtual") + "\" (expected auto, manual or off).");

        if (*newVirtualMode == VirtualMode::Manual)
            for (auto* e : xml->getChildWithTagNameIterator ("VirtualSpeaker"))
            {
                double x = 0.0, y = 0.0;

                if (! readNumber (*e, "x", x) || ! readNumber (*e, "y", y)
                    || (int) newManual.size() >= TriPanAudioProcessor::kMaxVirtualSpeakers)
                {
                    ++report.skipped;
                    continue;
                }

                const float cx = juce::jlimit (-range, range, (float) x);
                const float cy = juce::jlimit (-range, range, (float) y);
                if (! juce::approximatelyEqual (cx, (float) x) || ! juce::approximatelyEqual (cy, (float) y))
                    ++report.clamped;

                newManual.push_back ({ (double) cx, (double) cy });
            }
    }

    for (const auto& [index, pos] : entries)
    {
        writeParam (p, idFor (kind, index - 1, 'x'), pos.x);
        writeParam (p, idFor (kind, index - 1, 'y'), pos.y);
        if (pos.spread >= 0.0f)
        {
            writeParam (p, params::spreadId (index - 1), pos.spread);
            ++report.spreadApplied;
        }
        ++report.applied;
    }

    if (kind == Kind::Speakers)
    {
        // A speaker setup defines how many speakers there are: adjust "Number of Speakers" to match,
        // provided the track has that many outputs.
        report.fileCount = entries.rbegin()->first;
        report.trackOutputs = std::min (p.getTotalNumOutputChannels(), kMaxChannels);

        if (report.fileCount <= report.trackOutputs)
        {
            writeParam (p, params::kNumSpeakersId, (float) report.fileCount);
            report.speakersSetTo = report.fileCount;
        }
    }

    if (newVirtualMode)
    {
        p.setVirtualLayout (*newVirtualMode, *newVirtualMode == VirtualMode::Manual ? &newManual : nullptr);
        report.virtualMode = newVirtualMode;
        report.virtualCount = (int) newManual.size();
    }

    const int inUseLimit = kind == Kind::Sources ? p.getActiveSources() : p.getActiveSpeakers();
    for (const auto& entry : entries)
        if (entry.first <= inUseLimit)
            ++report.inUse;

    p.refreshTriangulation();
    return juce::Result::ok();
}
}
