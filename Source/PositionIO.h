#pragma once

#include "PluginProcessor.h"

/** Export and import of source or speaker positions as XML files.

    Sources and speakers use separate files:

        <TriPanSources version="1" count="3">
          <Source index="1" x="0.5" y="1.0"/> ...
        </TriPanSources>

        <TriPanSpeakers version="1" count="7">
          <Speaker index="1" x="0.0" y="5.0"/> ...
        </TriPanSpeakers>

    Export writes the sources/speakers currently in use. Import writes every valid entry (index
    1..64) to the corresponding parameters; it never changes the "Number of ..." settings.
    Message thread only. */
namespace tripan::io
{
enum class Kind { Sources, Speakers };

struct ImportReport
{
    int applied = 0;  // entries written to parameters
    int inUse = 0;    // of those, how many are currently in use (index <= active count)
    int clamped = 0;  // entries whose coordinates were limited to the allowed range
    int skipped = 0;  // malformed entries that were ignored
};

std::unique_ptr<juce::XmlElement> toXml (const TriPanAudioProcessor&, Kind);

juce::Result save (const TriPanAudioProcessor&, Kind, const juce::File&);

/** Reads and validates the whole file first; nothing is changed if it fails. */
juce::Result load (TriPanAudioProcessor&, Kind, const juce::File&, ImportReport&);

juce::String describe (Kind) ;
}
