// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file EditorTests.cpp
    @brief Editor tests for ViMiC: the shared controls are present or hidden as intended, the settings follow the parameters, the tables have the
    yaw and pattern columns and the word "Microphone", the pickup lobes; optional PNG snapshot (pass an output path as argv[1]). */

#include "../Source/PluginEditor.h"
#include "../Source/ViMiCParameters.h"
#include "vimic/Absorption.h"
#include "../Source/PluginProcessor.h"
#include "tlos/Parameters.h"

#include <cmath>
#include <cstdio>
#include <numbers>

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
static float value (ViMiCAudioProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

/** Sets a parameter to a real value. */
static void setParam (ViMiCAudioProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.getValueTreeState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

/** The area enclosed by a closed path of points (the lobe), by the shoelace formula. */
static double areaOf (const juce::Path& path)
{
    double area = 0.0;
    juce::Point<float> previous;
    bool first = true;
    juce::PathFlatteningIterator it (path);
    while (it.next())
    {
        area += (double) it.x1 * it.y2 - (double) it.x2 * it.y1;
        first = false;
    }
    (void) previous; (void) first;
    return std::abs (area) * 0.5;
}

/** Runs the editor tests; with argv[1] writes a PNG snapshot of the editor there. @return 0 if every check passed. */
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ViMiCAudioProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add  (juce::AudioChannelSet::discreteChannels (4));
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (6));
    CHECK (p.setBusesLayout (layout));
    p.setPlayConfigDetails (4, 6, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    std::unique_ptr<ViMiCAudioProcessorEditor> editor (static_cast<ViMiCAudioProcessorEditor*> (p.createEditor()));
    CHECK (editor != nullptr);
    editor->setSize (tlos::SpatialEditor::kBaseWidth, tlos::SpatialEditor::kBaseHeight);
    editor->syncControls();

    // The shared controls are there, the overlay toggle is not (the lobes are always drawn), and the outputs are called microphones.
    CHECK (! editor->getOverlayButton().isVisible());
    CHECK (editor->getShowLinksButton().isVisible() && editor->getResetButton().isVisible() && editor->getSettingsButton().isVisible());
    CHECK (editor->getExportSpeakersButton().getButtonText() == "Export microphones..." && editor->getImportSpeakersButton().getButtonText() == "Import microphones...");
    CHECK ((int) std::lround (editor->getSourceCountSlider().getValue()) == 4 && (int) std::lround (editor->getSpeakerCountSlider().getValue()) == 6);

    // The settings window: ViMiC's rows, no outside rolloff, polar coordinates.
    {
        auto& window = editor->showSettingsWindow();
        CHECK (window.isVisible() && window.getName() == "ViMiC - Settings");
        CHECK (! editor->getOutsideSlider().isVisible() && ! editor->getVirtualModeBox().isVisible());
        CHECK (editor->getSettingsContent().getCoordinateBox().getSelectedId() == 1);                 // Cartesian, as in the other plugins
        CHECK (editor->getSettingsContent().getRoomLabel().getText() == "Pad size" && editor->getSettingsContent().getRoomHeader().getText() == "Pad");     // the room of the reflections is further down

        auto& content = editor->getSettingsContent();

        // The settings are in tabs, one for each theme, and the window is as tall as the tallest one: no scrolling, and far shorter than the old single column.
        CHECK (content.getTabCount() == 6 && content.getTabTitle (0) == "Pad" && content.getTabTitle (1) == "Sound" && content.getTabTitle (2) == "Delay"
               && content.getTabTitle (3) == "Room" && content.getTabTitle (4) == "Walls" && content.getTabTitle (5) == "OSC");
        CHECK (content.getActiveTab() == 0 && content.getPreferredHeight() < 600 && ! window.isScrolling());
        CHECK (content.tabOf (&editor->getDistanceLawBox()) == 1 && content.tabOf (&editor->getDelayModeBox()) == 2 && content.tabOf (&editor->getReflectionBox()) == 3
               && content.tabOf (&editor->getAbsorptionPanel()) == 4 && content.tabOf (&content.getRoomSlider()) == 0 && content.tabOf (&content.getOscPanel()) == 5);
        CHECK (content.tabOf (editor->getSlider (vimic::params::kTemperatureId)) == 2 && content.tabOf (editor->getSlider (vimic::params::kRoomWidthId)) == 3
               && content.tabOf (editor->getSlider (vimic::params::kDirectGainId)) == 1 && content.tabOf (editor->getSlider (vimic::params::kMidHighId)) == 4);
        CHECK (content.tabOf (&content.getOutsideSlider()) == -1);                                   // not in any tab: ViMiC does not use it

        // Only the controls of the tab that shows are visible; a click on a tab button (setActiveTab) changes that.
        const auto show = [&] (juce::Component& c) { content.setActiveTab (content.tabOf (&c)); };
        show (content.getRoomSlider());
        CHECK (content.getRoomSlider().isVisible() && ! editor->getDistanceLawBox().isVisible() && ! editor->getAbsorptionPanel().isVisible());
        show (editor->getDistanceLawBox());
        CHECK (editor->getDistanceLawBox().isVisible() && ! content.getRoomSlider().isVisible() && content.getActiveTab() == 1);
        content.setActiveTab (99);                                                                  // limited to the last tab
        CHECK (content.getActiveTab() == 5 && content.getOscPanel().isVisible());
        show (editor->getDistanceLawBox());

        CHECK (editor->getDistanceLawBox().isVisible() && editor->getDistanceLawBox().getParentComponent() == &content);
        show (editor->getDelayModeBox());
        CHECK (editor->getDelayModeBox().isVisible());
        show (editor->getPolarityButton());
        CHECK (editor->getPolarityButton().isVisible());
        for (const auto& id : { vimic::params::kDistancePowerId, vimic::params::kDistanceDbId, vimic::params::kMinDistanceId, vimic::params::kMicPowerId, vimic::params::kSourcePowerId,
                                vimic::params::kTemperatureId,
                                vimic::params::kDirectGainId, vimic::params::kOutputGainId })
        {
            auto* slider = editor->getSlider (id);
            if (slider != nullptr)
                show (*slider);
            CHECK (slider != nullptr && slider->isVisible() && slider->getWidth() > 50 && slider->getParentComponent() == &content);
            if (slider != nullptr)
                CHECK (std::abs (slider->getValue() - (double) value (p, id)) < 1e-3);                   // shows the parameter
        }

        // The delay modes are one menu of four; the fade settings are only enabled in the cross-fade modes.
        CHECK (editor->getDelayModeBox().getNumItems() == 4 && editor->getDelayModeBox().getSelectedId() == 1);
        for (const auto& id : { vimic::params::kFadeThresholdId, vimic::params::kFadeTimeId })
        {
            auto* slider = editor->getSlider (id);
            if (slider != nullptr)
                show (*slider);
            CHECK (slider != nullptr && slider->isVisible() && slider->getWidth() > 50 && std::abs (slider->getValue() - (double) value (p, id)) < 0.01);
            CHECK (slider != nullptr && ! slider->isEnabled());
        }
        editor->getDelayModeBox().setSelectedId (4, juce::sendNotificationSync);
        CHECK (p.getDelayMode() == vimic::params::DelayMode::AbsoluteCrossFade);
        CHECK (editor->getSlider (vimic::params::kFadeTimeId)->isEnabled() && editor->getSlider (vimic::params::kFadeThresholdId)->isEnabled());
        setParam (p, vimic::params::kDelayModeId, 2.0f);                // a host change: Absolute, Doppler
        editor->updateFadeSliders();                                    // (a timer does this in a host)
        CHECK (editor->getDelayModeBox().getSelectedId() == 3 && ! editor->getSlider (vimic::params::kFadeTimeId)->isEnabled());
        setParam (p, vimic::params::kDelayModeId, 1.0f);                // Relative, cross-fade
        editor->updateFadeSliders();
        CHECK (editor->getSlider (vimic::params::kFadeTimeId)->isEnabled());
        setParam (p, vimic::params::kDelayModeId, 0.0f);
        editor->getSlider (vimic::params::kFadeTimeId)->setValue (120.0, juce::sendNotificationSync);
        CHECK (std::abs (value (p, vimic::params::kFadeTimeId) - 120.0f) < 0.5f);
        setParam (p, vimic::params::kFadeTimeId, 50.0f);

        // The room, the reflections and the walls.
        for (const auto& id : { vimic::params::kRoomWidthId, vimic::params::kRoomDepthId, vimic::params::kRoomHeightId, vimic::params::kRefl1GainId, vimic::params::kRefl2GainId,
                                vimic::params::kThresholdId, vimic::params::kAirCutoffId, vimic::params::kLowMidId, vimic::params::kMidHighId })
        {
            auto* slider = editor->getSlider (id);
            if (slider != nullptr)
                show (*slider);
            CHECK (slider != nullptr && slider->isVisible() && slider->getWidth() > 50 && slider->getParentComponent() == &content);
            if (slider != nullptr)
                CHECK (std::abs (slider->getValue() - (double) value (p, id)) < 1e-2 * std::max (1.0, std::abs ((double) value (p, id))));
        }
        show (editor->getReflectionBox());
        CHECK (editor->getReflectionBox().isVisible() && editor->getReflectionBox().getSelectedId() == 1);                       // Off
        editor->getReflectionBox().setSelectedId (3, juce::sendNotificationSync);
        CHECK (p.getRoomSettings().order == 2);
        setParam (p, vimic::params::kReflOrderId, 1.0f);
        CHECK (editor->getReflectionBox().getSelectedId() == 2);
        CHECK (editor->getSlider (vimic::params::kThresholdId)->getTextFromValue (vimic::params::kThresholdOff) == "off");
        editor->getSlider (vimic::params::kRoomWidthId)->setValue (14.0, juce::sendNotificationSync);
        CHECK (std::abs (p.getRoomSettings().room.width - 14.0) < 1e-3);

        // The absorption panel: sliders attached to the parameters; a preset writes three parameters; any other values show "custom".
        auto& panel = editor->getAbsorptionPanel();
        show (panel);
        CHECK (panel.isVisible() && panel.getParentComponent() == &content && panel.getHeight() == AbsorptionPanel::kHeight && panel.getWidth() >= 400);
        CHECK (std::abs (panel.getSlider (vimic::Floor, 2).getValue() - 8.0) < 0.05);
        panel.getSlider (vimic::Left, 0).setValue (33.0, juce::sendNotificationSync);
        CHECK (std::abs (value (p, vimic::params::absorptionId (vimic::Left, 0)) - 33.0f) < 0.05f);
        panel.refresh();
        CHECK (panel.getPresetBox (vimic::Left).getSelectedId() == 1 && panel.getPresetBox (vimic::Right).getSelectedId() > 1);   // custom; drywall
        const auto* carpet = vimic::findSurfacePreset ("carpet");
        int carpetId = 0;
        for (int id = 2; id < 2 + (int) vimic::surfacePresets().size(); ++id)
            if (panel.getPresetBox (vimic::Left).getItemText (panel.getPresetBox (vimic::Left).indexOfItemId (id)) == "carpet")
                carpetId = id;
        CHECK (carpet != nullptr && carpetId > 0);
        panel.getPresetBox (vimic::Rear).setSelectedId (carpetId, juce::sendNotificationSync);
        CHECK (std::abs (value (p, vimic::params::absorptionId (vimic::Rear, 0)) - 6.0f) < 0.05f && std::abs (value (p, vimic::params::absorptionId (vimic::Rear, 2)) - 63.0f) < 0.05f);
        CHECK (std::abs (panel.getSlider (vimic::Rear, 1).getValue() - 37.0) < 0.05);                                  // the slider follows
        panel.getAllBox().setSelectedId (carpetId, juce::sendNotificationSync);                                        // all walls
        for (int wall = 0; wall < vimic::WallCount; ++wall)
            CHECK (std::abs (value (p, vimic::params::absorptionId (wall, 1)) - 37.0f) < 0.05f && panel.getPresetBox (wall).getSelectedId() == carpetId);
        for (int wall = 0; wall < vimic::WallCount; ++wall)
            panel.applyPreset (wall, *vimic::findSurfacePreset (wall < 4 ? "drywall" : wall == 4 ? "wood" : "plaster"));    // the defaults again
        setParam (p, vimic::params::kRoomWidthId, 10.0f);
        setParam (p, vimic::params::kReflOrderId, 0.0f);

        // The controls write to the parameters, and a host change moves them.
        editor->getSlider (vimic::params::kTemperatureId)->setValue (5.0, juce::sendNotificationSync);
        CHECK (std::abs (value (p, vimic::params::kTemperatureId) - 5.0f) < 1e-3f);
        editor->getDistanceLawBox().setSelectedId (2, juce::sendNotificationSync);
        CHECK (value (p, vimic::params::kDistanceLawId) > 0.5f);
        setParam (p, vimic::params::kDistanceLawId, 0.0f);
        CHECK (editor->getDistanceLawBox().getSelectedId() == 1);
        editor->getDelayModeBox().setSelectedId (3, juce::sendNotificationSync);
        CHECK (p.getDelayMode() == vimic::params::DelayMode::AbsoluteDoppler);
        editor->getPolarityButton().setToggleState (false, juce::sendNotificationSync);
        CHECK (value (p, vimic::params::kMicPolarityId) < 0.5f);
        setParam (p, vimic::params::kMicPolarityId, 1.0f);
        setParam (p, vimic::params::kDelayModeId, 0.0f);
        setParam (p, vimic::params::kTemperatureId, 20.0f);

        if (argc > 1)
            for (int tab = 0; tab < content.getTabCount(); ++tab)
            {
                content.setActiveTab (tab);
                juce::File out (juce::String (argv[1]).replace (".png", "-settings-" + content.getTabTitle (tab).toLowerCase() + ".png"));
                out.deleteFile();
                juce::FileOutputStream stream (out);
                juce::PNGImageFormat png;
                CHECK (stream.openedOk() && png.writeImageToStream (content.createComponentSnapshot (content.getLocalBounds(), true, 1.0f), stream));
                std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
            }
        content.setActiveTab (0);
        window.setVisible (false);
    }

    // The tables: yaw and pattern for the microphones, no spread for the sources, and the word "Microphones".
    {
        auto& sources = editor->showTableWindow (tlos::io::Kind::Sources);
        auto& speakers = editor->showTableWindow (tlos::io::Kind::Speakers);
        CHECK (speakers.getName() == "ViMiC - Microphones" && sources.getName() == "ViMiC - Sources");
        CHECK (! speakers.getContent().getVirtualModeBox().isShowing() && ! speakers.getContent().getVirtualModeBox().isVisible());          // no virtual speakers in ViMiC
        CHECK (speakers.getContent().getVirtualTable() == nullptr);
        auto& sourceHeader = sources.getContent().getMainTable().getTable().getHeader();
        auto& micHeader = speakers.getContent().getMainTable().getTable().getHeader();
        CHECK (sourceHeader.getNumColumns (true) == 11 && micHeader.getNumColumns (true) == 11);      // sources: no Spread, but Height, Yaw, Pitch and Pattern; microphones: the same
        CHECK (sourceHeader.getColumnName (tlos::PositionTable::extraColumn).startsWith ("Height") && sourceHeader.getColumnName (tlos::PositionTable::extraColumn + 1).startsWith ("Yaw")
           && sourceHeader.getColumnName (tlos::PositionTable::extraColumn + 2).startsWith ("Pitch") && sourceHeader.getColumnName (tlos::PositionTable::extraColumn + 3) == "Pattern");
        CHECK (micHeader.getColumnName (tlos::PositionTable::extraColumn).startsWith ("Height") && micHeader.getColumnName (tlos::PositionTable::extraColumn + 1).startsWith ("Yaw")
           && micHeader.getColumnName (tlos::PositionTable::extraColumn + 2).startsWith ("Pitch") && micHeader.getColumnName (tlos::PositionTable::extraColumn + 3) == "Pattern");

        auto& table = speakers.getContent().getMainTable();
        CHECK (table.setValueFromText (1, tlos::PositionTable::extraColumn + 1, "45") == "45.0" && std::abs (value (p, vimic::params::micYawId (1)) - 45.0f) < 1e-3f);
        CHECK (table.setValueFromText (1, tlos::PositionTable::extraColumn + 1, "500") == "180.0");                                    // limited to +-180
        CHECK (table.setValueFromText (1, tlos::PositionTable::extraColumn + 3, "0.3") == "0.30" && std::abs (value (p, vimic::params::micPatternId (1)) - 0.3f) < 1e-4f);
        CHECK (table.setValueFromText (1, tlos::PositionTable::extraColumn + 3, "7") == "1.00");                                   // limited to 0..1
        CHECK (table.setValueFromText (1, tlos::PositionTable::extraColumn + 3, "much") == "1.00");                                // rubbish is rejected
        setParam (p, vimic::params::micYawId (1), 0.0f);
        setParam (p, vimic::params::micPatternId (1), 0.5f);

        // The sources table has them as well.
        auto& sourceTable = sources.getContent().getMainTable();
        CHECK (sourceTable.getValue (0, tlos::PositionTable::extraColumn + 3) == 1.0 && sourceTable.getValue (0, tlos::PositionTable::extraColumn) == 1.5);                                                 // a source is omni by default
        CHECK (sourceTable.setValueFromText (0, tlos::PositionTable::extraColumn + 1, "-60") == "-60.0" && std::abs (value (p, vimic::params::srcYawId (0)) + 60.0f) < 1e-3f);
        CHECK (sourceTable.setValueFromText (0, tlos::PositionTable::extraColumn + 3, "0.5") == "0.50" && std::abs (value (p, vimic::params::srcPatternId (0)) - 0.5f) < 1e-4f);
        CHECK (sourceTable.setValueFromText (0, tlos::PositionTable::extraColumn + 3, "9") == "1.00");
        CHECK (sourceTable.setValueFromText (0, tlos::PositionTable::extraColumn, "4.5") == "4.50" && std::abs (value (p, vimic::params::srcHeightId (0)) - 4.5f) < 1e-3f);
        CHECK (sourceTable.setValueFromText (0, tlos::PositionTable::extraColumn + 2, "120") == "90.0");                           // pitch is limited to +-90
        CHECK (table.setValueFromText (1, tlos::PositionTable::extraColumn + 2, "-30") == "-30.0" && std::abs (value (p, vimic::params::micPitchId (1)) + 30.0f) < 1e-3f);
        CHECK (table.setValueFromText (1, tlos::PositionTable::extraColumn, "-2") == "0.00");                                      // the floor is the lowest height
        setParam (p, vimic::params::srcHeightId (0), 1.5f);
        setParam (p, vimic::params::srcPitchId (0), 0.0f);
        setParam (p, vimic::params::micPitchId (1), 0.0f);
        setParam (p, vimic::params::micHeightId (1), 1.5f);
        setParam (p, vimic::params::srcYawId (0), 0.0f);
        setParam (p, vimic::params::srcPatternId (0), 1.0f);
    }

    // The spread has no use: a spread drag (Option) on a source does nothing, and its circle is never drawn.
    auto& field = editor->getField();
    field.refresh();
    const auto start = field.toView ((double) value (p, tlos::params::sourceId (0, 'x')), (double) value (p, tlos::params::sourceId (0, 'y')));
    juce::ModifierKeys option (juce::ModifierKeys::altModifier);
    field.pressAt (start, option);
    field.dragTo (start + juce::Point<float> (0.0f, -60.0f));
    field.release();
    CHECK (value (p, tlos::params::spreadId (0)) < 1e-6f);

    // A source with a pattern gets a lobe and an arrow on the pad; paint the pad with one (no crash), and the links follow the source's direction.
    setParam (p, vimic::params::srcPatternId (1), 0.3f);
    setParam (p, vimic::params::srcYawId (1), 40.0f);
    field.refresh();
    {
        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }
    setParam (p, vimic::params::srcPatternId (1), 1.0f);
    setParam (p, vimic::params::srcYawId (1), 0.0f);

    // The pad drags a source through the parameters, as in the other plugins, and shows its position in the polar mode.
    field.pressAt (start, juce::ModifierKeys());
    field.dragTo (field.toView (3.0, -4.0));
    field.release();
    CHECK (std::abs (value (p, tlos::params::sourceId (0, 'x')) - 3.0f) < 0.2f && std::abs (value (p, tlos::params::sourceId (0, 'y')) + 4.0f) < 0.2f);
    CHECK (field.positionText ({ true, 0 }).startsWith ("x ") && field.nameOf ({ false, 2 }) == "Microphone 3");
    p.setCoordinateMode (tlos::SpatialProcessor::CoordinateMode::Polar);
    CHECK (field.positionText ({ true, 0 }).startsWith ("azimuth"));
    p.setCoordinateMode (tlos::SpatialProcessor::CoordinateMode::Cartesian);

    // The pickup lobes: the area follows the pattern (omni a circle, cardioid 3/8 of it).
    const double circle = std::numbers::pi * 100.0 * 100.0;
    CHECK (std::abs (areaOf (ViMiCField::lobePath (1.0, 1, true, 100.0f)) / circle - 1.0) < 0.01);
    CHECK (std::abs (areaOf (ViMiCField::lobePath (0.5, 1, true, 100.0f)) / circle - 0.375) < 0.01);          // cardioid: 3/8 of the circle
    CHECK (areaOf (ViMiCField::lobePath (0.0, 1, true, 100.0f)) < areaOf (ViMiCField::lobePath (0.0, 1, false, 100.0f)) * 0.55);   // a restricted figure-8 has half the lobes
    CHECK (areaOf (ViMiCField::lobePath (0.5, 3, true, 100.0f)) < areaOf (ViMiCField::lobePath (0.5, 1, true, 100.0f)));          // a larger exponent is sharper

    // The pad paints (lobes, links) in both coordinate modes without a problem.
    for (auto mode : { tlos::SpatialProcessor::CoordinateMode::Polar, tlos::SpatialProcessor::CoordinateMode::Cartesian })
    {
        p.setCoordinateMode (mode);
        field.refresh();
        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }
    p.setCoordinateMode (tlos::SpatialProcessor::CoordinateMode::Cartesian);

    // The room is drawn on the pad while the reflections are on (paints without a problem, in both coordinate modes).
    setParam (p, vimic::params::kReflOrderId, 1.0f);
    setParam (p, vimic::params::kRoomWidthId, 14.0f);
    setParam (p, vimic::params::kRoomDepthId, 9.0f);
    field.refresh();
    for (auto mode : { tlos::SpatialProcessor::CoordinateMode::Polar, tlos::SpatialProcessor::CoordinateMode::Cartesian })
    {
        p.setCoordinateMode (mode);
        field.refresh();
        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }
    p.setCoordinateMode (tlos::SpatialProcessor::CoordinateMode::Polar);

    // For the picture: two sources with a radiation pattern (a cardioid and a sharper one).
    setParam (p, vimic::params::srcPatternId (1), 0.5f);
    setParam (p, vimic::params::srcYawId (1), 40.0f);
    setParam (p, vimic::params::srcPatternId (2), 0.0f);
    setParam (p, vimic::params::srcYawId (2), -120.0f);

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
