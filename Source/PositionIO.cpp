#include "PositionIO.h"
#include "Parameters.h"

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

struct Entry
{
    float x, y;
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

    for (int i = 0; i < count; ++i)
    {
        auto* e = root->createNewChildElement (entryTag (kind));
        e->setAttribute ("index", juce::String (i + 1).paddedLeft (' ', 2)); // padded like the coordinates
        e->setAttribute ("x", formatCoordinate (readParam (p, idFor (kind, i, 'x'))));
        e->setAttribute ("y", formatCoordinate (readParam (p, idFor (kind, i, 'y'))));
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
        const double x = e->getDoubleAttribute ("x", std::nan (""));
        const double y = e->getDoubleAttribute ("y", std::nan (""));

        if (index < 1 || index > kMaxChannels || ! e->hasAttribute ("x") || ! e->hasAttribute ("y")
            || ! std::isfinite (x) || ! std::isfinite (y))
        {
            ++report.skipped;
            continue;
        }

        const float cx = juce::jlimit (-range, range, (float) x);
        const float cy = juce::jlimit (-range, range, (float) y);
        if (! juce::approximatelyEqual (cx, (float) x) || ! juce::approximatelyEqual (cy, (float) y))
            ++report.clamped;

        entries[index] = { cx, cy };
    }

    if (entries.empty())
        return juce::Result::fail ("The file contains no valid " + describe (kind) + " positions.");

    const int inUseLimit = kind == Kind::Sources ? p.getActiveSources() : p.getActiveSpeakers();
    for (const auto& [index, pos] : entries)
    {
        writeParam (p, idFor (kind, index - 1, 'x'), pos.x);
        writeParam (p, idFor (kind, index - 1, 'y'), pos.y);
        ++report.applied;
        if (index <= inUseLimit)
            ++report.inUse;
    }

    p.refreshTriangulation();
    return juce::Result::ok();
}
}
