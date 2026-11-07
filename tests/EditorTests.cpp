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
    editor->setSize (680, 740);
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
