#pragma once

#include "PluginProcessor.h"

#include <optional>

/** Export and import of source or speaker positions as XML files.

    Sources and speakers use separate files:

        <TriPanSources version="1" count="3">
          <Source index="1" x="0.5" y="1.0" spread="0.0"/> ...
        </TriPanSources>

    The optional attribute spread (metres, 0 to 10) is the source's spread radius. An entry without it
    leaves that source's spread unchanged.

        <TriPanSpeakers version="1" count="7" virtual="manual">
          <Speaker index="1" x="0.0" y="5.0"/> ...
          <VirtualSpeaker index="1" x="0.0" y="0.0"/> ...      (only with virtual="manual")
        </TriPanSpeakers>

    The optional attribute virtual ("auto", "manual" or "off") records the virtual (imaginary) speaker
    setting of the layout. Auto and off need nothing more: the automatic speakers are recomputed from
    the real ones. Manual lists the user's own virtual speakers. A file without the attribute (older
    files) leaves the current virtual speaker setting alone.

    Export writes the sources/speakers currently in use. Import writes every valid entry (index
    1..64) to the corresponding parameters. Importing speakers also sets "Number of Speakers" to the
    number of speakers in the file (its highest index), but only if the track has that many
    outputs; otherwise the setting is left alone. Importing sources never changes "Number of Sources".
    Message thread only. */
namespace tripan::io
{
enum class Kind { Sources, Speakers };

struct ImportReport
{
    int applied = 0;  // entries written to parameters
    int inUse = 0;    // of those, how many are in use afterwards (index <= active count)
    int fileCount = 0;        // speakers in the file (highest valid index); speaker imports only
    int speakersSetTo = 0;    // new "Number of Speakers" if it was adjusted, otherwise 0
    int trackOutputs = 0;     // outputs available on the track; speaker imports only
    int clamped = 0;  // entries whose coordinates were limited to the allowed range
    int skipped = 0;  // malformed entries that were ignored
    int spreadApplied = 0; // source imports: entries that also set the spread

    std::optional<VirtualMode> virtualMode; // speaker imports: set if the file specified the virtual speakers
    int virtualCount = 0;                   // manual virtual speakers applied from the file
};

std::unique_ptr<juce::XmlElement> toXml (const TriPanAudioProcessor&, Kind);

juce::Result save (const TriPanAudioProcessor&, Kind, const juce::File&);

/** Reads and validates the whole file first; nothing is changed if it fails. */
juce::Result load (TriPanAudioProcessor&, Kind, const juce::File&, ImportReport&);

juce::String describe (Kind) ;
}
