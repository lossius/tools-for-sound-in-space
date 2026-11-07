// Editor tests: hit-testing, dragging through the parameters, and an optional PNG snapshot
// (pass an output path as argv[1]).
#include "../Source/Parameters.h"
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (! (cond)) {                                                      \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

static float value (TriPanAudioProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

static void setParam (TriPanAudioProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.getValueTreeState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    TriPanAudioProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add  (juce::AudioChannelSet::discreteChannels (3));
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (7));
    CHECK (p.setBusesLayout (layout));
    p.setPlayConfigDetails (3, 7, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    // Make the layout a little irregular so the picture is more interesting.
    setParam (p, tripan::params::speakerId (2, 'x'), 6.5f);
    setParam (p, tripan::params::speakerId (2, 'y'), -1.0f);
    setParam (p, tripan::params::speakerId (5, 'x'), -3.0f);
    setParam (p, tripan::params::speakerId (5, 'y'), -2.0f);
    setParam (p, tripan::params::sourceId (0, 'x'), 0.5f);
    setParam (p, tripan::params::sourceId (0, 'y'), 1.0f);
    setParam (p, tripan::params::sourceId (1, 'x'), -6.5f);
    setParam (p, tripan::params::sourceId (1, 'y'), 8.5f); // outside the hull
    setParam (p, tripan::params::sourceId (2, 'x'), 3.0f);
    setParam (p, tripan::params::sourceId (2, 'y'), -3.5f);
    p.refreshTriangulation();

    CHECK (p.getEditMode() == EditMode::Sources); // Mode 1 is the default

    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* editor = dynamic_cast<TriPanAudioProcessorEditor*> (base.get());
    CHECK (editor != nullptr);
    editor->setSize (TriPanAudioProcessorEditor::kBaseWidth, TriPanAudioProcessorEditor::kBaseHeight);
    auto& field = editor->getField();

    // Count controls: their range follows the track's channel counts, while the stored
    // parameter keeps its value (64) until the user changes it.
    {
        auto& srcSlider = editor->getSourceCountSlider();
        auto& spkSlider = editor->getSpeakerCountSlider();
        CHECK (srcSlider.getMinimum() == 1.0 && srcSlider.getMaximum() == 3.0 && srcSlider.getValue() == 3.0);
        CHECK (spkSlider.getMinimum() == 1.0 && spkSlider.getMaximum() == 7.0 && spkSlider.getValue() == 7.0);
        CHECK (std::abs (value (p, tripan::params::kNumSpeakersId) - 64.0f) < 1e-3f);

        spkSlider.setValue (5.0, juce::sendNotificationSync); // as if the user changed it
        CHECK (std::abs (value (p, tripan::params::kNumSpeakersId) - 5.0f) < 1e-3f);
        CHECK (p.getActiveSpeakers() == 5);

        setParam (p, tripan::params::kNumSourcesId, 2.0f); // as if automation changed it
        editor->syncControls();
        CHECK (srcSlider.getValue() == 2.0 && p.getActiveSources() == 2);

        // Inactive channels cannot be grabbed.
        field.refresh();
        CHECK (! field.findHandleAt (field.toView (6.5, -1.0)).has_value() || field.findHandleAt (field.toView (6.5, -1.0))->index < 5);

        // Back to everything in use for the remaining checks.
        spkSlider.setValue (7.0, juce::sendNotificationSync);
        srcSlider.setValue (3.0, juce::sendNotificationSync);
        CHECK (p.getActiveSpeakers() == 7 && p.getActiveSources() == 3);
    }

    // Edit modes: the menu selects them, and they decide what can be grabbed, moved and reset.
    {
        auto& box = editor->getModeBox();
        CHECK (box.getSelectedId() == 1 && box.getNumItems() == 4);

        for (int id = 1; id <= 4; ++id)
        {
            box.setSelectedId (id, juce::sendNotificationSync);
            const auto mode = (EditMode) id;
            CHECK (p.getEditMode() == mode);

            const auto srcPos = field.toView (0.5, 1.0);   // source 1
            const auto spkPos = field.toView (6.5, -1.0);  // speaker 3 (moved earlier in this test)

            const auto hs = field.findHandleAt (srcPos);
            CHECK (hs.has_value() == canEditSources (mode));
            const auto hk = field.findHandleAt (spkPos);
            CHECK (hk.has_value() == canEditSpeakers (mode));

            field.moveHandle ({ true, 0 }, field.toView (2.0, 2.0));
            CHECK ((std::abs (value (p, tripan::params::sourceId (0, 'x')) - 2.0f) < 0.02f) == canEditSources (mode));
            field.moveHandle ({ false, 2 }, field.toView (-2.0, -2.0));
            CHECK ((std::abs (value (p, tripan::params::speakerId (2, 'x')) + 2.0f) < 0.02f) == canEditSpeakers (mode));

            // restore
            setParam (p, tripan::params::sourceId (0, 'x'), 0.5f);
            setParam (p, tripan::params::sourceId (0, 'y'), 1.0f);
            setParam (p, tripan::params::speakerId (2, 'x'), 6.5f);
            setParam (p, tripan::params::speakerId (2, 'y'), -1.0f);

            CHECK (editor->getResetButton().isEnabled() == (mode != EditMode::View));
        }

        // Reset layout only touches what the mode allows.
        box.setSelectedId (1, juce::sendNotificationSync); // sources unlocked, speakers locked
        setParam (p, tripan::params::speakerId (0, 'x'), 1.234f);
        setParam (p, tripan::params::sourceId (0, 'x'), 7.0f);
        editor->getResetButton().onClick(); // (triggerClick needs a visible button)
        CHECK (std::abs (value (p, tripan::params::speakerId (0, 'x')) - 1.234f) < 1e-3f); // untouched
        CHECK (std::abs (value (p, tripan::params::sourceId (0, 'x')) - 7.0f) > 0.5f);     // reset

        // Restore the picture and leave the mode at "both" for the checks below.
        setParam (p, tripan::params::speakerId (0, 'x'), 0.0f);
        setParam (p, tripan::params::sourceId (0, 'x'), 0.5f);
        setParam (p, tripan::params::sourceId (0, 'y'), 1.0f);
        box.setSelectedId (3, juce::sendNotificationSync);
    }

    // Export / import of positions as XML (mode is "both" here).
    {
        using tripan::io::Kind;
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tripan_io_test");
        dir.deleteRecursively();
        dir.createDirectory();
        const auto srcFile = dir.getChildFile ("sources.xml");
        const auto spkFile = dir.getChildFile ("speakers.xml");

        CHECK (editor->exportPositionsTo (Kind::Sources, srcFile).wasOk());
        CHECK (editor->exportPositionsTo (Kind::Speakers, spkFile).wasOk());

        // Separate files with their own root elements and one entry per channel in use.
        const auto srcXml = juce::XmlDocument::parse (srcFile);
        const auto spkXml = juce::XmlDocument::parse (spkFile);
        CHECK (srcXml != nullptr && srcXml->hasTagName ("TriPanSources") && srcXml->getNumChildElements() == 3);
        CHECK (spkXml != nullptr && spkXml->hasTagName ("TriPanSpeakers") && spkXml->getNumChildElements() == 7);

        // Positions are exported rounded to 3 decimal places (and never as "-0.000").
        {
            const float keepX = value (p, tripan::params::sourceId (0, 'x')), keepY = value (p, tripan::params::sourceId (0, 'y'));
            setParam (p, tripan::params::sourceId (0, 'x'), 1.23456f);
            setParam (p, tripan::params::sourceId (0, 'y'), -0.0004f);
            const auto rounded = dir.getChildFile ("rounded.xml");
            CHECK (editor->exportPositionsTo (Kind::Sources, rounded).wasOk());
            const auto doc = juce::XmlDocument::parse (rounded);
            const auto* first = doc != nullptr ? doc->getChildElement (0) : nullptr;
            CHECK (first != nullptr && first->getStringAttribute ("x").trim() == "1.235" && first->getStringAttribute ("y").trim() == "0.000");
            for (auto* e : doc->getChildIterator())
                for (const auto* attribute : { "x", "y" })
                {
                    const auto text = e->getStringAttribute (attribute);
                    CHECK (text.fromFirstOccurrenceOf (".", false, false).length() == 3);
                    CHECK (text.length() == 7); // fixed width: positive and negative values align
                }

            // In the file every entry line has the same length and the decimal points line up.
            const auto lines = juce::StringArray::fromLines (rounded.loadFileAsString());
            int entryLines = 0;
            int pointColumn = -1;
            for (const auto& line : lines)
                if (line.contains ("<Source "))
                {
                    ++entryLines;
                    const int column = line.indexOfChar ('.');
                    if (pointColumn < 0) pointColumn = column;
                    CHECK (column == pointColumn);
                    CHECK (line.length() == lines[lines.indexOf (line)].length());
                }
            if (argc > 1)
                std::printf ("--- exported sample ---\n%s\n---\n", rounded.loadFileAsString().toRawUTF8());
            CHECK (entryLines == 3);
            {
                int length = -1; bool same = true;
                for (const auto& line : lines)
                    if (line.contains ("<Source ")) { if (length < 0) length = line.length(); same = same && line.length() == length; }
                CHECK (same);
            }

            setParam (p, tripan::params::sourceId (0, 'x'), keepX);
            setParam (p, tripan::params::sourceId (0, 'y'), keepY);
            CHECK (editor->exportPositionsTo (Kind::Sources, srcFile).wasOk()); // re-export the original positions
        }

        // Round trip: remember, scramble, import, compare.
        float srcX[3], srcY[3], spkX[7], spkY[7];
        for (int i = 0; i < 3; ++i) { srcX[i] = value (p, tripan::params::sourceId (i, 'x'));  srcY[i] = value (p, tripan::params::sourceId (i, 'y')); }
        for (int i = 0; i < 7; ++i) { spkX[i] = value (p, tripan::params::speakerId (i, 'x')); spkY[i] = value (p, tripan::params::speakerId (i, 'y')); }

        for (int i = 0; i < 3; ++i) { setParam (p, tripan::params::sourceId (i, 'x'), -9.0f);  setParam (p, tripan::params::sourceId (i, 'y'), 9.0f); }
        for (int i = 0; i < 7; ++i) { setParam (p, tripan::params::speakerId (i, 'x'), -9.0f); setParam (p, tripan::params::speakerId (i, 'y'), 9.0f); }

        CHECK (editor->importPositionsFrom (Kind::Sources, srcFile).wasOk());
        for (int i = 0; i < 3; ++i)
        {
            CHECK (std::abs (value (p, tripan::params::sourceId (i, 'x')) - srcX[i]) < 1e-3f);
            CHECK (std::abs (value (p, tripan::params::sourceId (i, 'y')) - srcY[i]) < 1e-3f);
        }
        // Importing sources did not touch the speakers.
        CHECK (std::abs (value (p, tripan::params::speakerId (0, 'x')) + 9.0f) < 1e-3f);

        CHECK (editor->importPositionsFrom (Kind::Speakers, spkFile).wasOk());
        for (int i = 0; i < 7; ++i)
        {
            CHECK (std::abs (value (p, tripan::params::speakerId (i, 'x')) - spkX[i]) < 1e-3f);
            CHECK (std::abs (value (p, tripan::params::speakerId (i, 'y')) - spkY[i]) < 1e-3f);
        }

        // A file of the other kind, a missing file and garbage are rejected without changing anything.
        setParam (p, tripan::params::speakerId (0, 'x'), 3.0f);
        const auto wrongKind = editor->importPositionsFrom (Kind::Speakers, srcFile);
        CHECK (wrongKind.failed() && wrongKind.getErrorMessage().containsIgnoreCase ("source"));
        CHECK (editor->importPositionsFrom (Kind::Sources, spkFile).failed());
        CHECK (editor->importPositionsFrom (Kind::Sources, dir.getChildFile ("missing.xml")).failed());
        const auto garbage = dir.getChildFile ("garbage.xml");
        garbage.replaceWithText ("<TriPanSources><Source index=");
        CHECK (editor->importPositionsFrom (Kind::Sources, garbage).failed());
        const auto empty = dir.getChildFile ("empty.xml");
        empty.replaceWithText ("<TriPanSpeakers version=\"1\"><Speaker index=\"1\" x=\"1\"/></TriPanSpeakers>");
        CHECK (editor->importPositionsFrom (Kind::Speakers, empty).failed());
        CHECK (std::abs (value (p, tripan::params::speakerId (0, 'x')) - 3.0f) < 1e-3f);

        // Partial files: valid, out-of-range (clamped), malformed (skipped), and more entries than the
        // 7 speakers in use (applied, but not counted as in use). The count setting is untouched.
        const auto mixed = dir.getChildFile ("mixed.xml");
        juce::String text = "<TriPanSpeakers version=\"1\"><Speaker index=\"2\" x=\"1.5\" y=\"-2.5\"/>"
                            "<Speaker index=\"3\" x=\"99\" y=\"0\"/><Speaker index=\"4\" x=\"1\"/>"
                            "<Speaker index=\"12\" x=\"4\" y=\"4\"/></TriPanSpeakers>";
        mixed.replaceWithText (text);
        tripan::io::ImportReport report;
        CHECK (tripan::io::load (p, Kind::Speakers, mixed, report).wasOk());
        CHECK (report.applied == 3 && report.inUse == 2 && report.clamped == 1 && report.skipped == 1);
        CHECK (std::abs (value (p, tripan::params::speakerId (1, 'x')) - 1.5f) < 1e-3f);
        CHECK (std::abs (value (p, tripan::params::speakerId (2, 'x')) - 10.0f) < 1e-3f);
        CHECK (std::abs (value (p, tripan::params::speakerId (11, 'y')) - 4.0f) < 1e-3f);
        CHECK (p.getActiveSpeakers() == 7);

        // Importing a speaker file with 3 speakers sets Number of Speakers to 3 (the track has 7 outputs)
        // and the control follows.
        const auto three = dir.getChildFile ("three.xml");
        three.replaceWithText ("<TriPanSpeakers version=\"1\"><Speaker index=\"1\" x=\"-3\" y=\"3\"/>"
                               "<Speaker index=\"2\" x=\"3\" y=\"3\"/><Speaker index=\"3\" x=\"0\" y=\"-4\"/></TriPanSpeakers>");
        CHECK (editor->importPositionsFrom (Kind::Speakers, three).wasOk());
        CHECK (p.getActiveSpeakers() == 3);
        CHECK (editor->getSpeakerCountSlider().getValue() == 3.0);
        setParam (p, tripan::params::kNumSpeakersId, 7.0f); // back to all 7
        editor->syncControls();
        CHECK (editor->getSpeakerCountSlider().getValue() == 7.0);

        // Restore the picture.
        for (int i = 0; i < 7; ++i) { setParam (p, tripan::params::speakerId (i, 'x'), spkX[i]); setParam (p, tripan::params::speakerId (i, 'y'), spkY[i]); }
        setParam (p, tripan::params::speakerId (11, 'y'), 0.0f);
        setParam (p, tripan::params::speakerId (11, 'x'), 0.0f);

        // Import follows the edit-mode locks; export is always available.
        auto& box = editor->getModeBox();
        for (int id = 1; id <= 4; ++id)
        {
            box.setSelectedId (id, juce::sendNotificationSync);
            const auto mode = (EditMode) id;
            CHECK (editor->getImportSourcesButton().isEnabled() == canEditSources (mode));
            CHECK (editor->getImportSpeakersButton().isEnabled() == canEditSpeakers (mode));
            CHECK (editor->getExportSourcesButton().isEnabled() && editor->getExportSpeakersButton().isEnabled());
        }
        box.setSelectedId (3, juce::sendNotificationSync);

        dir.deleteRecursively();
    }

    // Scalable GUI: the base size is laid out once and scaled uniformly by the window size.
    {
        const auto baseW = TriPanAudioProcessorEditor::kBaseWidth, baseH = TriPanAudioProcessorEditor::kBaseHeight;
        CHECK (editor->getWidth() == baseW && editor->getHeight() == baseH);
        CHECK (std::abs (editor->getScale() - 1.0f) < 1e-6f);

        const auto unscaled = field.toView (5.0, 0.0).getDistanceFrom (field.toView (0.0, 0.0));
        const auto onScreen = [&] (double x, double y) { return editor->getLocalPoint (&field, field.toView (x, y)); };

        const auto baseDistance = onScreen (5.0, 0.0).getDistanceFrom (onScreen (0.0, 0.0));
        CHECK (std::abs (baseDistance - unscaled) < 0.01f);

        for (double factor : { 1.5, 2.0, 3.0 })
        {
            editor->setSize (juce::roundToInt (baseW * factor), juce::roundToInt (baseH * factor));
            CHECK (std::abs (editor->getScale() - (float) factor) < 0.01f);
            CHECK (std::abs (onScreen (5.0, 0.0).getDistanceFrom (onScreen (0.0, 0.0)) - unscaled * factor) < 0.05 * factor);

            // Everything stays inside the window and the field is still operable through the transform:
            const auto bounds = editor->getLocalArea (&field, field.getLocalBounds());
            CHECK (editor->getLocalBounds().contains (bounds));
            const auto screenPos = onScreen (0.5, 1.0); // source 1
            CHECK (field.getLocalPoint (editor, screenPos).getDistanceFrom (field.toView (0.5, 1.0)) < 0.05f);
        }

        // A host that ignores the aspect ratio gets a centred, uniformly scaled GUI.
        editor->setSize (baseW * 2, baseH * 2 + 400);
        CHECK (std::abs (editor->getScale() - 2.0f) < 0.01f);
        const auto topLeft = editor->getLocalArea (&field, field.getLocalBounds()).getY();
        CHECK (topLeft >= 200);

        // The chosen size is remembered with the state and restored for a new editor.
        editor->setSize (baseW * 2, baseH * 2);
        CHECK (p.getEditorWidth() == baseW * 2);
        {
            std::unique_ptr<juce::AudioProcessorEditor> again (p.createEditor());
            CHECK (again->getWidth() == baseW * 2 && again->getHeight() == baseH * 2);
        }
        p.setEditorWidth (50); // too small: clamped to the base size
        { std::unique_ptr<juce::AudioProcessorEditor> again (p.createEditor()); CHECK (again->getWidth() == baseW); }
        p.setEditorWidth (100000); // too large: clamped to the maximum
        { std::unique_ptr<juce::AudioProcessorEditor> again (p.createEditor()); CHECK (again->getWidth() == baseW * TriPanAudioProcessorEditor::kMaxScale); }

        editor->setSize (baseW, baseH);
    }

    // Hit test: a source and a speaker are found at their own positions; empty space finds nothing.
    {
        const auto s0 = field.toView (0.5, 1.0);
        const auto h = field.findHandleAt (s0);
        CHECK (h && h->isSource && h->index == 0);

        const auto sp2 = field.toView (6.5, -1.0);
        const auto h2 = field.findHandleAt (sp2 + juce::Point<float> (3.0f, -2.0f));
        CHECK (h2 && ! h2->isSource && h2->index == 2);

        CHECK (! field.findHandleAt (field.toView (9.0, 9.0)).has_value());
        // Inactive channels (source 4 and above) are not hit-testable even though their parameters exist.
        setParam (p, tripan::params::sourceId (3, 'x'), -9.0f);
        setParam (p, tripan::params::sourceId (3, 'y'), -9.0f);
        CHECK (! field.findHandleAt (field.toView (-9.0, -9.0)).has_value());
    }

    // Drag speaker 2 to a new place via the component's own mapping; parameters follow.
    {
        const auto target = field.toView (-2.0, 7.0);
        field.moveHandle ({ false, 2 }, target);
        CHECK (std::abs (value (p, tripan::params::speakerId (2, 'x')) + 2.0f) < 0.02f);
        CHECK (std::abs (value (p, tripan::params::speakerId (2, 'y')) - 7.0f) < 0.02f);

        // Dragging far outside the plane clamps to the parameter range (screen y grows downwards).
        field.moveHandle ({ true, 0 }, juce::Point<float> (-5000.0f, 5000.0f));
        CHECK (std::abs (value (p, tripan::params::sourceId (0, 'x')) + 10.0f) < 1e-3f);
        CHECK (std::abs (value (p, tripan::params::sourceId (0, 'y')) + 10.0f) < 1e-3f);

        // Round trip of the coordinate mapping.
        const auto v = field.toView (3.25, -4.5);
        field.moveHandle ({ true, 1 }, v);
        CHECK (std::abs (value (p, tripan::params::sourceId (1, 'x')) - 3.25f) < 0.02f);
        CHECK (std::abs (value (p, tripan::params::sourceId (1, 'y')) + 4.5f) < 0.02f);
    }

    // Restore a pleasant picture for the snapshot.
    setParam (p, tripan::params::speakerId (2, 'x'), 6.5f);
    setParam (p, tripan::params::speakerId (2, 'y'), -1.0f);
    setParam (p, tripan::params::sourceId (0, 'x'), 0.5f);
    setParam (p, tripan::params::sourceId (0, 'y'), 1.0f);
    setParam (p, tripan::params::sourceId (1, 'x'), -6.5f);
    setParam (p, tripan::params::sourceId (1, 'y'), 8.5f);

    // The snapshot shows 6 of 7 speakers in use.
    setParam (p, tripan::params::kNumSpeakersId, 6.0f);
    p.refreshTriangulation();
    editor->syncControls();

    editor->setSize (1050, 1215); // snapshot at 1.5x

    if (argc > 1)
    {
        field.refresh();
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::File out (argv[1]);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        CHECK (stream.openedOk() && png.writeImageToStream (image, stream));
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    }

    base.reset();

    if (failures == 0)
        std::printf ("All editor tests passed\n");
    return failures == 0 ? 0 : 1;
}
