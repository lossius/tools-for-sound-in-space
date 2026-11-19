// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file EditorTests.cpp
    @brief Editor tests for DistPan: the shared controls are present or hidden as intended, the pad drags through the
    parameters, the rolloff and blur sliders follow their parameters; optional PNG snapshot (pass an output path as argv[1]). */

#include "../Source/DistPanParameters.h"
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "tlos/Parameters.h"

#include <cmath>
#include <cstdio>

/** Number of failed checks so far; the exit code of main(). */
static int failures = 0;

/** Counts and prints a failed condition, and carries on with the next check. */
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (! (cond)) {                                                      \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

/** Current real value of a parameter. */
static float value (DistPanAudioProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

/** Sets a parameter to a real value. */
static void setParam (DistPanAudioProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.getValueTreeState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

/** True if DistPan's two sliders are children of the settings content. */
static bool rolloffInSettings (DistPanAudioProcessorEditor& editor)
{
    return editor.getRolloffSlider().getParentComponent() == &editor.getSettingsContent()
        && editor.getBlurSlider().getParentComponent() == &editor.getSettingsContent();
}

/** Runs the editor tests; with argv[1] writes a PNG snapshot of the editor there. @return 0 if every check passed. */
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    DistPanAudioProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add  (juce::AudioChannelSet::discreteChannels (4));
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (6));
    CHECK (p.setBusesLayout (layout));
    p.setPlayConfigDetails (4, 6, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    std::unique_ptr<DistPanAudioProcessorEditor> editor (static_cast<DistPanAudioProcessorEditor*> (p.createEditor()));
    CHECK (editor != nullptr);
    editor->setSize (tlos::SpatialEditor::kBaseWidth, tlos::SpatialEditor::kBaseHeight);
    editor->syncControls();

    // What this plugin does not have is hidden: there is no overlay to toggle and no virtual speakers.
    CHECK (! editor->getOverlayButton().isVisible());                    // the hull is always shown: no toggle
    CHECK (editor->getField().getShowOverlay());
    CHECK (editor->getShowLinksButton().isVisible() && editor->getResetButton().isVisible());

    // The shared controls work: counts, edit modes.
    CHECK ((int) std::lround (editor->getSourceCountSlider().getValue()) == 4);
    CHECK ((int) std::lround (editor->getSpeakerCountSlider().getValue()) == 6);
    editor->getSpeakersModeBox().setSelectedId (1, juce::sendNotificationSync);
    CHECK (p.canEditSpeakers());
    editor->getSpeakersModeBox().setSelectedId (2, juce::sendNotificationSync);

    // The main window has the Settings button and none of what moved into the settings window.
    CHECK (editor->getSettingsButton().isVisible() && editor->getSettingsButton().getRight() <= tlos::SpatialEditor::kBaseWidth);
    CHECK (! editor->getRoomSlider().isShowing() && ! editor->getOutsideSlider().isShowing());     // not in the plugin window
    CHECK (editor->getRoomSlider().getParentComponent() == &editor->getSettingsContent());
    CHECK (editor->getSettingsWindow() == nullptr);

    // The settings window: DistPan has no virtual speakers, but its own two sliders (under Panning), and writes a picture.
    {
        auto& window = editor->showSettingsWindow();
        CHECK (window.isVisible() && window.getName() == "DistPan - Settings");
        CHECK (! editor->getVirtualModeBox().isVisible());
        CHECK (rolloffInSettings (*editor) && editor->getRolloffSlider().isVisible() && editor->getBlurSlider().isVisible());
        if (argc > 1)
        {
            auto& settings = editor->getSettingsContent();
            juce::File out (juce::String (argv[1]).replace (".png", "-settings.png"));
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat png;
            CHECK (stream.openedOk() && png.writeImageToStream (settings.createComponentSnapshot (settings.getLocalBounds(), true, 1.0f), stream));
            std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
        }
        window.setVisible (false);
    }

    // The shared outside rolloff slider is there, shows the default and writes to its parameter.
    auto& outside = editor->getOutsideSlider();
    CHECK (outside.isVisible() && outside.getWidth() > 50);
    CHECK (std::abs (outside.getValue() - (double) tlos::params::kDefaultOutsideRolloff) < 1e-3);
    outside.setValue (12.0, juce::sendNotificationSync);
    CHECK (std::abs (value (p, tlos::params::kOutsideRolloffId) - 12.0f) < 1e-3f);
    setParam (p, tlos::params::kOutsideRolloffId, 6.0f);

    // The sliders show the parameters and write to them.
    auto& rolloff = editor->getRolloffSlider();
    auto& blur = editor->getBlurSlider();
    CHECK (std::abs (rolloff.getValue() - (double) distpan::params::kDefaultRolloff) < 1e-3);
    CHECK (std::abs (blur.getValue() - (double) distpan::params::kDefaultBlur) < 1e-3);
    CHECK (rolloff.isVisible() && blur.isVisible());
    CHECK (rolloff.getWidth() > 50 && blur.getWidth() > 50);

    rolloff.setValue (9.0, juce::sendNotificationSync);
    blur.setValue (1.5, juce::sendNotificationSync);
    CHECK (std::abs (value (p, distpan::params::kRolloffId) - 9.0f) < 1e-3f);
    CHECK (std::abs (value (p, distpan::params::kBlurId) - 1.5f) < 1e-3f);

    setParam (p, distpan::params::kRolloffId, 4.0f); // a host change moves the slider
    CHECK (std::abs (rolloff.getValue() - 4.0) < 1e-3);

    CHECK (rolloff.getParentComponent() == &editor->getSettingsContent() && blur.getParentComponent() == &editor->getSettingsContent());
    CHECK (rolloff.getBottom() <= blur.getY());                                   // one row below the other

    // The pad drags a source through the parameters, as in TriPan.
    auto& field = editor->getField();
    field.refresh();
    const auto start = field.toView ((double) value (p, tlos::params::sourceId (0, 'x')), (double) value (p, tlos::params::sourceId (0, 'y')));
    const auto target = field.toView (3.0, -4.0);
    field.pressAt (start, juce::ModifierKeys());
    field.dragTo (target);
    field.release();
    CHECK (std::abs (value (p, tlos::params::sourceId (0, 'x')) - 3.0f) < 0.2f);
    CHECK (std::abs (value (p, tlos::params::sourceId (0, 'y')) + 4.0f) < 0.2f);

    // Speakers are locked in View: pressing on one moves nothing.
    const float spk0x = value (p, tlos::params::speakerId (0, 'x'));
    const auto spkView = field.toView ((double) spk0x, (double) value (p, tlos::params::speakerId (0, 'y')));
    field.pressAt (spkView, juce::ModifierKeys());
    field.dragTo (spkView + juce::Point<float> (40.0f, 40.0f));
    field.release();
    CHECK (std::abs (value (p, tlos::params::speakerId (0, 'x')) - spk0x) < 1e-6f);

    // Mute, solo and exclude from the pad's own functions (what the right-click menu calls); the tables have the columns.
    field.toggleMute ({ true, 1 });
    CHECK (value (p, tlos::params::sourceMuteId (1)) == 1.0f);
    field.toggleMute ({ true, 1 });
    field.toggleSolo ({ false, 2 });
    CHECK (value (p, tlos::params::speakerSoloId (2)) == 1.0f);
    field.toggleSolo ({ false, 2 });
    field.toggleExclude (4);
    CHECK (p.isSpeakerExcluded (4) && p.getIncludedSpeakers().positions.size() == 5);
    field.refresh();
    field.toggleExclude (4);
    CHECK (! p.isSpeakerExcluded (4));
    {
        auto& speakers = editor->showTableWindow (tlos::io::Kind::Speakers).getContent();
        CHECK (speakers.getMainTable().getTable().getHeader().getNumColumns (true) == 8);        // #, X, Y, Trim, Mute, Solo, Excl., Weight
        CHECK (speakers.getMainTable().getValue (1, tlos::PositionTable::extraColumn) == 1.0);
        CHECK (speakers.getMainTable().setValueFromText (1, tlos::PositionTable::extraColumn, "0.4") == "0.40");
        CHECK (std::abs (value (p, distpan::params::speakerWeightId (1)) - 0.4f) < 1e-4f);
        CHECK (speakers.getMainTable().setValueFromText (1, tlos::PositionTable::extraColumn, "5") == "1.00");   // limited to 0..1
        CHECK (speakers.getMainTable().setValueFromText (1, tlos::PositionTable::extraColumn, "-2") == "0.00");
        CHECK (speakers.getMainTable().setValueFromText (1, tlos::PositionTable::extraColumn, "much") == "0.00");   // rubbish is rejected
        setParam (p, distpan::params::speakerWeightId (1), 1.0f);
        CHECK (editor->showTableWindow (tlos::io::Kind::Sources).getContent().getMainTable().getTable().getHeader().getNumColumns (true) == 8);
    }

    // Links can be switched off, and the pad paints with them on (no crash with many sources and speakers).
    editor->getShowLinksButton().setToggleState (false, juce::sendNotificationSync);
    CHECK (! field.getShowLinks());
    editor->getShowLinksButton().setToggleState (true, juce::sendNotificationSync);
    CHECK (field.getShowLinks());
    juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    {
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }

    // The window is called DistPan in the table windows, and exports are named after the plugin.
    auto& table = editor->showTableWindow (tlos::io::Kind::Sources);
    CHECK (! editor->showTableWindow (tlos::io::Kind::Speakers).getContent().getVirtualModeBox().isVisible());     // no virtual speakers here, so no menu for them
    CHECK (table.getName().startsWith ("DistPan"));

    if (argc > 1)
    {
        juce::File out (argv[1]);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        CHECK (stream.openedOk() && png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), stream));
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    }

    editor.reset();

    if (failures == 0)
        std::printf ("All editor tests passed\n");
    return failures == 0 ? 0 : 1;
}
