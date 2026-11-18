// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file EditorTests.cpp
    @brief Editor tests for VecPan: the shared controls are present or hidden as intended, the pad drags through the
    parameters, the pad shows the speaker pairs, VBAP links and the polar grid; optional PNG snapshot (pass an output path as argv[1]). */

#include "../Source/PluginEditor.h"
#include "../Source/VecPanParameters.h"
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
static float value (VecPanAudioProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

/** Sets a parameter to a real value. */
static void setParam (VecPanAudioProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.getValueTreeState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

/** Runs the editor tests; with argv[1] writes a PNG snapshot of the editor there. @return 0 if every check passed. */
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    VecPanAudioProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add  (juce::AudioChannelSet::discreteChannels (4));
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (6));
    CHECK (p.setBusesLayout (layout));
    p.setPlayConfigDetails (4, 6, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    std::unique_ptr<VecPanAudioProcessorEditor> editor (static_cast<VecPanAudioProcessorEditor*> (p.createEditor()));
    CHECK (editor != nullptr);
    editor->setSize (tlos::SpatialEditor::kBaseWidth, tlos::SpatialEditor::kBaseHeight);
    editor->syncControls();

    // The overlay toggle (the speaker pairs) is there; there are no virtual speakers.
    CHECK (editor->getOverlayButton().isVisible() && editor->getOverlayButton().getButtonText() == "Show pairs");
    CHECK (editor->getShowLinksButton().isVisible() && editor->getResetButton().isVisible());
    CHECK (editor->getSettingsButton().isVisible() && editor->getSettingsButton().getRight() <= tlos::SpatialEditor::kBaseWidth);

    // The shared controls work: counts, edit modes.
    CHECK ((int) std::lround (editor->getSourceCountSlider().getValue()) == 4);
    CHECK ((int) std::lround (editor->getSpeakerCountSlider().getValue()) == 6);
    editor->getSpeakersModeBox().setSelectedId (1, juce::sendNotificationSync);
    CHECK (p.canEditSpeakers());
    editor->getSpeakersModeBox().setSelectedId (2, juce::sendNotificationSync);

    // The settings window has VecPan's centre radius row (under Panning), and starts in polar coordinates.
    {
        auto& window = editor->showSettingsWindow();
        CHECK (window.isVisible() && window.getName() == "VecPan - Settings");
        CHECK (! editor->getVirtualModeBox().isVisible());
        CHECK (editor->getSettingsContent().getCoordinateBox().getSelectedId() == 2);
        auto& centre = editor->getCentreRadiusSlider();
        CHECK (centre.isVisible() && centre.getParentComponent() == &editor->getSettingsContent());
        CHECK (std::abs (centre.getValue() - (double) vecpan::params::kDefaultCentreRadius) < 1e-3);
        centre.setValue (1.5, juce::sendNotificationSync);
        CHECK (std::abs (value (p, vecpan::params::kCentreRadiusId) - 1.5f) < 1e-3f);
        setParam (p, vecpan::params::kCentreRadiusId, 0.5f);                       // a host change moves the slider
        CHECK (std::abs (centre.getValue() - 0.5) < 1e-3);

        // The "Place speakers on circle" button moves the speakers to the farthest one's distance (one step, whatever the Edit/View menu says).
        auto& circle = editor->getCircleButton();
        CHECK (circle.isVisible() && circle.getParentComponent() == &editor->getSettingsContent());
        setParam (p, tlos::params::speakerId (1, 'x'), 3.0f);                      // speaker 2 closer in, speaker 1 stays at 5 m
        setParam (p, tlos::params::speakerId (1, 'y'), 0.0f);
        const auto radius = [&] (int i) { return std::hypot (value (p, tlos::params::speakerId (i, 'x')), value (p, tlos::params::speakerId (i, 'y'))); };
        CHECK (std::abs (radius (1) - 3.0) < 1e-3 && std::abs (radius (0) - 5.0) < 1e-3);
        circle.onClick();
        CHECK (std::abs (radius (1) - radius (0)) < 1e-3 && std::abs (radius (0) - 5.0) < 1e-3 && std::abs (value (p, tlos::params::speakerId (1, 'y'))) < 1e-4f);
        window.setVisible (false);
    }

    // The shared outside rolloff slider is there and writes to its parameter.
    auto& outside = editor->getOutsideSlider();
    outside.setValue (12.0, juce::sendNotificationSync);
    CHECK (std::abs (value (p, tlos::params::kOutsideRolloffId) - 12.0f) < 1e-3f);
    setParam (p, tlos::params::kOutsideRolloffId, 6.0f);

    // The pad drags a source through the parameters, as in the other plugins.
    auto& field = editor->getField();
    field.refresh();
    const auto start = field.toView ((double) value (p, tlos::params::sourceId (0, 'x')), (double) value (p, tlos::params::sourceId (0, 'y')));
    const auto target = field.toView (3.0, -4.0);
    field.pressAt (start, juce::ModifierKeys());
    field.dragTo (target);
    field.release();
    CHECK (std::abs (value (p, tlos::params::sourceId (0, 'x')) - 3.0f) < 0.2f);
    CHECK (std::abs (value (p, tlos::params::sourceId (0, 'y')) + 4.0f) < 0.2f);

    // The pad's position text is polar by default.
    CHECK (field.positionText ({ true, 0 }).startsWith ("azimuth"));
    p.setCoordinateMode (tlos::SpatialProcessor::CoordinateMode::Cartesian);
    CHECK (field.positionText ({ true, 0 }).startsWith ("x "));
    p.setCoordinateMode (tlos::SpatialProcessor::CoordinateMode::Polar);

    // Mute, solo and exclude from the pad's own functions; the pad paints with the pairs and links (no crash), in both coordinate modes.
    field.toggleMute ({ true, 1 });
    CHECK (value (p, tlos::params::sourceMuteId (1)) == 1.0f);
    field.toggleMute ({ true, 1 });
    field.toggleExclude (4);
    CHECK (p.isSpeakerExcluded (4) && p.getIncludedSpeakers().positions.size() == 5);
    field.refresh();
    field.toggleExclude (4);
    editor->getOverlayButton().setToggleState (true, juce::sendNotificationSync);
    CHECK (field.getShowOverlay());
    for (auto mode : { tlos::SpatialProcessor::CoordinateMode::Polar, tlos::SpatialProcessor::CoordinateMode::Cartesian })
    {
        p.setCoordinateMode (mode);
        field.refresh();
        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }
    p.setCoordinateMode (tlos::SpatialProcessor::CoordinateMode::Polar);

    // The table windows are named after the plugin, and show azimuth and distance.
    auto& table = editor->showTableWindow (tlos::io::Kind::Sources);
    CHECK (table.getName().startsWith ("VecPan"));
    CHECK (table.getContent().getMainTable().getTable().getHeader().getColumnName (tlos::PositionTable::xColumn).startsWith ("Azimuth"));

    if (argc > 1)
    {
        field.refresh();
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
