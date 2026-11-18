// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PositionIO.cpp
    @brief XML writing and strict, all-or-nothing reading of position files. */

#include "tlos/PositionIO.h"
#include "tlos/Polar.h"
#include "tlos/Parameters.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace tlos::io
{
namespace
{
/** Value of the `version` attribute written to every file. */
constexpr int kFormatVersion = 1;

/** Ending of the root element's name for the kind of file; the start is the name of the plugin that wrote it. */
const char* tagSuffix (Kind k) { return k == Kind::Sources ? "Sources" : "Speakers"; }
/** Name of the root element a plugin writes, for example "TriPanSources". */
juce::String rootTag (const SpatialProcessor& p, Kind k) { return p.getName() + tagSuffix (k); }
/** True if the document holds the kind of positions, whichever plugin wrote it: files can be moved between the plugins. */
bool hasKindTag (const juce::XmlElement& xml, Kind k) { return xml.getTagName().endsWith (tagSuffix (k)); }
/** Name of the per-entry elements for the kind of file. */
const char* entryTag (Kind k) { return k == Kind::Sources ? "Source" : "Speaker"; }

/** Parameter ID of the x or y (`axis`) coordinate of entry `index` (0-based). */
juce::String idFor (Kind k, int index, char axis)
{
    return k == Kind::Sources ? params::sourceId (index, axis) : params::speakerId (index, axis);
}

/** Current real value of a parameter. */
float readParam (const SpatialProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

/** Sets a parameter to a real value inside a change gesture, so that hosts record it. */
void writeParam (SpatialProcessor& p, const juce::String& id, float value)
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

/** Reads a position from an element: `x` and `y` if both are numbers, otherwise `az` (degrees, counter-clockwise from the
    front) and `dist` (metres). Returns false if neither pair is there. */
bool readPosition (const juce::XmlElement& e, double& x, double& y)
{
    if (readNumber (e, "x", x) && readNumber (e, "y", y))
        return true;

    double azimuth = 0.0, distance = 0.0;
    if (! readNumber (e, "az", azimuth) || ! readNumber (e, "dist", distance) || distance < 0.0)
        return false;

    const auto p = fromPolar (azimuth, distance);
    x = p.x;
    y = p.y;
    return true;
}

/** One validated entry of an imported file. */
struct Entry
{
    float x;                ///< Position x in metres.
    float y;                ///< Position y in metres.
    float spread = -1.0f;   ///< Spread in metres; negative if the file has none.
    int group = -1;         ///< Group number; negative if the file has none.
    bool hasTrim = false;   ///< Whether the file gives a trim.
    float trim = 0.0f;      ///< Trim in dB, if hasTrim.
    int mute = -1;          ///< 1 = muted, 0 = not; negative if the file has none.
    int solo = -1;          ///< 1 = soloed, 0 = not; negative if the file has none.
    int excluded = -1;      ///< Speakers: 1 = excluded from the panning, 0 = not; negative if the file has none.
    bool hasExtra = false;  ///< Speakers: whether the file gives the plugin's own per-speaker value (SpatialProcessor::getSpeakerColumn()).
    float extra = 0.0f;     ///< That value, if hasExtra.
};

/** Formats a trim in dB for a file (one decimal, padded like the coordinates). */
juce::String formatTrim (float db)
{
    auto text = juce::String (db, 1);
    if (text == "-0.0")
        text = "0.0";
    return text.paddedLeft (' ', 5);
}

/** Reads a 0/1 attribute into `out`. @return false if the attribute is there but is neither 0 nor 1. */
bool readSwitch (const juce::XmlElement& e, const char* name, int& out)
{
    if (! e.hasAttribute (name))
        return true;

    double v = 0.0;
    if (! readNumber (e, name, v) || (v != 0.0 && v != 1.0))
        return false;

    out = (int) v;
    return true;
}
}

juce::String describe (Kind k)
{
    return k == Kind::Sources ? "source" : "speaker";
}

std::unique_ptr<juce::XmlElement> toXml (const SpatialProcessor& p, Kind kind)
{
    const int count = kind == Kind::Sources ? p.getActiveSources() : p.getActiveSpeakers();

    auto root = std::make_unique<juce::XmlElement> (rootTag (p, kind));
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

    const auto groups = kind == Kind::Sources ? p.getSourceGroups() : tlos::SourceGroups();
    const auto excluded = p.getExcludedSpeakers();

    // Trim, mute, solo and exclude are written for every entry as soon as one of them is not at its default.
    const auto idTrim = [&] (int i) { return kind == Kind::Sources ? params::sourceTrimId (i) : params::speakerTrimId (i); };
    const auto idMute = [&] (int i) { return kind == Kind::Sources ? params::sourceMuteId (i) : params::speakerMuteId (i); };
    const auto idSolo = [&] (int i) { return kind == Kind::Sources ? params::sourceSoloId (i) : params::speakerSoloId (i); };
    bool anyTrim = false, anyMute = false, anySolo = false, anyExcluded = false;
    for (int i = 0; i < count; ++i)
    {
        anyTrim = anyTrim || std::abs (readParam (p, idTrim (i))) >= 0.05f;
        anyMute = anyMute || readParam (p, idMute (i)) >= 0.5f;
        anySolo = anySolo || readParam (p, idSolo (i)) >= 0.5f;
        anyExcluded = anyExcluded || (kind == Kind::Speakers && excluded[(size_t) i]);
    }
    const auto column = kind == Kind::Speakers ? p.getSpeakerColumn() : std::nullopt;
    bool anyExtra = false;
    for (int i = 0; column && i < count; ++i)
        anyExtra = anyExtra || std::abs (readParam (p, column->idFor (i)) - (float) column->defaultValue) > 0.0005f;

    for (int i = 0; i < count; ++i)
    {
        auto* e = root->createNewChildElement (entryTag (kind));
        e->setAttribute ("index", juce::String (i + 1).paddedLeft (' ', 2)); // padded like the coordinates
        e->setAttribute ("x", formatCoordinate (readParam (p, idFor (kind, i, 'x'))));
        e->setAttribute ("y", formatCoordinate (readParam (p, idFor (kind, i, 'y'))));
        if (kind == Kind::Sources)
            e->setAttribute ("spread", formatCoordinate (readParam (p, params::spreadId (i))));
        if (groups.any())
            e->setAttribute ("group", groups.groupOf (i));
        if (anyTrim)
            e->setAttribute ("trim", formatTrim (readParam (p, idTrim (i))));
        if (anyMute)
            e->setAttribute ("mute", readParam (p, idMute (i)) >= 0.5f ? 1 : 0);
        if (anySolo)
            e->setAttribute ("solo", readParam (p, idSolo (i)) >= 0.5f ? 1 : 0);
        if (anyExcluded)
            e->setAttribute ("excluded", excluded[(size_t) i] ? 1 : 0);
        if (anyExtra)
            e->setAttribute (column->attribute, juce::String (readParam (p, column->idFor (i)), 3).paddedLeft (' ', 6));
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

juce::Result save (const SpatialProcessor& p, Kind kind, const juce::File& file)
{
    const auto xml = toXml (p, kind);

    // A long wrap length keeps every entry on one line, so the columns stay aligned.
    juce::XmlElement::TextFormat format;
    format.lineWrapLength = 200;

    if (! file.replaceWithText (xml->toString (format)))
        return juce::Result::fail ("Could not write to " + file.getFullPathName());

    return juce::Result::ok();
}

juce::Result load (SpatialProcessor& p, Kind kind, const juce::File& file, ImportReport& report)
{
    report = {};

    if (! file.existsAsFile())
        return juce::Result::fail ("The file does not exist.");

    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return juce::Result::fail ("The file is not valid XML.");

    if (! hasKindTag (*xml, kind))
    {
        const auto other = kind == Kind::Sources ? Kind::Speakers : Kind::Sources;
        if (hasKindTag (*xml, other))
            return juce::Result::fail ("This file contains " + describe (other) + " positions, not " + describe (kind) + " positions.");
        return juce::Result::fail ("This is not a " + describe (kind) + " positions file.");
    }

    if (xml->getIntAttribute ("version", 1) > kFormatVersion)
        return juce::Result::fail ("The file was written by a newer version of " + p.getName() + ".");

    // Validate everything first, so a bad file changes nothing. Later entries win on duplicate indices.
    std::map<int, Entry> entries;
    int ordinal = 0;
    const float range = params::kCoordRange;

    for (auto* e : xml->getChildWithTagNameIterator (entryTag (kind)))
    {
        ++ordinal;
        const int index = e->getIntAttribute ("index", ordinal);
        double x = 0.0, y = 0.0;

        if (index < 1 || index > kMaxChannels || ! readPosition (*e, x, y))
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

        if (kind == Kind::Sources && e->hasAttribute ("group"))
        {
            double group = 0.0;
            if (readNumber (*e, "group", group) && group >= 0.0 && group <= SourceGroups::kMaxGroupId && group == std::floor (group))
                entry.group = (int) group;
            else
                ++report.skipped;
        }

        if (e->hasAttribute ("trim"))
        {
            double trim = 0.0;
            if (readNumber (*e, "trim", trim))
            {
                entry.hasTrim = true;
                entry.trim = juce::jlimit (params::kMinTrimDb, params::kMaxTrimDb, (float) trim);
                if (! juce::approximatelyEqual (entry.trim, (float) trim))
                    ++report.clamped;
            }
            else
                ++report.skipped;
        }

        if (const auto column = kind == Kind::Speakers ? p.getSpeakerColumn() : std::nullopt; column && e->hasAttribute (column->attribute))
        {
            double v = 0.0;
            if (readNumber (*e, column->attribute.toRawUTF8(), v))
            {
                entry.hasExtra = true;
                entry.extra = (float) juce::jlimit (column->minimum, column->maximum, v);
                if (! juce::approximatelyEqual (entry.extra, (float) v))
                    ++report.clamped;
            }
            else
                ++report.skipped;
        }

        if (! readSwitch (*e, "mute", entry.mute) || ! readSwitch (*e, "solo", entry.solo)
            || (kind == Kind::Speakers && ! readSwitch (*e, "excluded", entry.excluded)))
            ++report.skipped;

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

                if (! readPosition (*e, x, y)
                    || (int) newManual.size() >= SpatialProcessor::kMaxVirtualSpeakers)
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

    if (kind == Kind::Sources
        && std::any_of (entries.begin(), entries.end(), [] (const auto& entry) { return entry.second.group >= 0; }))
    {
        SourceGroups::Ids ids {};
        for (const auto& [index, pos] : entries)
            ids[(size_t) index - 1] = std::max (pos.group, 0);

        const SourceGroups imported (ids);
        p.setSourceGroups (imported);
        for (int id = 1; id <= SourceGroups::kMaxGroupId; ++id)
            if (imported.sizeOf (id) > 0)
                ++report.groupCount;
    }

    // Trim, mute, solo and exclude: if any entry of the file has one of them, the file defines it for every
    // channel (those it does not mention go back to the default), like the groups.
    {
        const auto any = [&] (auto has) { return std::any_of (entries.begin(), entries.end(), [&] (const auto& entry) { return has (entry.second); }); };
        const auto idTrim = [&] (int i) { return kind == Kind::Sources ? params::sourceTrimId (i) : params::speakerTrimId (i); };
        const auto idMute = [&] (int i) { return kind == Kind::Sources ? params::sourceMuteId (i) : params::speakerMuteId (i); };
        const auto idSolo = [&] (int i) { return kind == Kind::Sources ? params::sourceSoloId (i) : params::speakerSoloId (i); };

        const bool anyTrim = any ([] (const Entry& x) { return x.hasTrim; });
        const bool anyMute = any ([] (const Entry& x) { return x.mute >= 0; });
        const bool anySolo = any ([] (const Entry& x) { return x.solo >= 0; });
        const bool anyExcluded = kind == Kind::Speakers && any ([] (const Entry& x) { return x.excluded >= 0; });

        for (int i = 0; i < kMaxChannels; ++i)
        {
            const auto found = entries.find (i + 1);
            const Entry* entry = found != entries.end() ? &found->second : nullptr;

            if (anyTrim) writeParam (p, idTrim (i), entry != nullptr && entry->hasTrim ? entry->trim : 0.0f);
            if (anyMute) writeParam (p, idMute (i), entry != nullptr && entry->mute == 1 ? 1.0f : 0.0f);
            if (anySolo) writeParam (p, idSolo (i), entry != nullptr && entry->solo == 1 ? 1.0f : 0.0f);
        }

        const auto column = kind == Kind::Speakers ? p.getSpeakerColumn() : std::nullopt;
        if (column && any ([] (const Entry& x) { return x.hasExtra; }))
            for (int i = 0; i < kMaxChannels; ++i)
            {
                const auto found = entries.find (i + 1);
                writeParam (p, column->idFor (i), found != entries.end() && found->second.hasExtra ? found->second.extra : (float) column->defaultValue);
            }

        if (anyExcluded)
        {
            std::array<bool, kMaxChannels> excluded {};
            for (const auto& [index, entry] : entries)
                excluded[(size_t) index - 1] = entry.excluded == 1;
            p.setExcludedSpeakers (excluded);
        }

        for (const auto& [index, entry] : entries)
            if (entry.hasTrim || entry.mute >= 0 || entry.solo >= 0 || entry.excluded >= 0 || entry.hasExtra)
                ++report.calibrationApplied;
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

    p.refreshLayout();
    return juce::Result::ok();
}
}
