// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PositionIO.h
    @brief XML export and import of source and speaker positions. */

#pragma once

#include "PluginProcessor.h"

#include <optional>

/** Export and import of source or speaker positions as XML files.

    Sources and speakers use separate files:

        <TriPanSources version="1" count="3">
          <Source index="1" x="0.5" y="1.0" spread="0.0"/> ...
        </TriPanSources>

    The optional attribute spread (metres, 0 to 10) is the source's spread radius. An entry without it
    leaves that source's spread unchanged. The optional attribute group (1 to 32) says which group the
    source belongs to (0 or missing: none); sources with the same number move together. Export writes it
    when any source is grouped. An import where at least one entry has the attribute replaces the
    groups with the file's (sources not in the file end up ungrouped); a file without it leaves the
    groups as they are.

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
/** Which positions a file holds. */
enum class Kind
{
    Sources,  ///< Source positions (and spreads).
    Speakers  ///< Speaker positions and the virtual speaker setting.
};

/** What an import did, for the message shown to the user. */
struct ImportReport
{
    int applied = 0;          ///< Entries written to parameters.
    int inUse = 0;            ///< Of those, how many are in use afterwards (index <= active count).
    int fileCount = 0;        ///< Speakers in the file (highest valid index); speaker imports only.
    int speakersSetTo = 0;    ///< New "Number of Speakers" if it was adjusted, otherwise 0.
    int trackOutputs = 0;     ///< Outputs available on the track; speaker imports only.
    int clamped = 0;          ///< Entries whose coordinates were limited to the allowed range.
    int skipped = 0;          ///< Malformed entries that were ignored.
    int spreadApplied = 0;    ///< Source imports: entries that also set the spread.
    int groupCount = 0;       ///< Source imports: number of groups the file defined (0 if it had no group information).

    std::optional<VirtualMode> virtualMode; ///< Speaker imports: set if the file specified the virtual speakers.
    int virtualCount = 0;                   ///< Manual virtual speakers applied from the file.
};

/** Builds the XML document for the sources or speakers currently in use. */
std::unique_ptr<juce::XmlElement> toXml (const TriPanAudioProcessor& processor, Kind kind);

/** Writes toXml() to a file (coordinates rounded to 3 decimals and padded to a fixed width).
    @return Success, or an error message if the file could not be written. */
juce::Result save (const TriPanAudioProcessor& processor, Kind kind, const juce::File& file);

/** Reads and validates the whole file first; nothing is changed if it fails.
    @param processor  The processor whose parameters are written.
    @param kind       Which kind of file is expected.
    @param file       The file to read.
    @param report     Filled in with what was done (only valid on success).
    @return           Success, or an error message describing what is wrong with the file. */
juce::Result load (TriPanAudioProcessor& processor, Kind kind, const juce::File& file, ImportReport& report);

/** @return The singular word used for the kind in messages ("source" or "speaker"). */
juce::String describe (Kind);
}
