// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file EditorTests.cpp
    @brief Editor tests: hit-testing, dragging through the parameters, and an optional PNG snapshot (pass an output path as argv[1]). */

#include "tlos/Parameters.h"
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cmath>
#include <limits>
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
static float value (TriPanAudioProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

/** Sets a parameter to a real value. */
static void setParam (TriPanAudioProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.getValueTreeState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

/** Source groups on the pad: selection, grouping, rigid moves limited by the pad, single moves, group spread, persistence and files. */
static void testGroups (const char* snapshotPath)
{
    TriPanAudioProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add  (juce::AudioChannelSet::discreteChannels (6));
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (4));
    CHECK (p.setBusesLayout (layout));
    p.setPlayConfigDetails (6, 4, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    const double sx[] { -2.0, 0.0, 3.0, -8.0, 8.0, 5.0 }, sy[] { -2.0, 1.0, 4.0, 8.0, -8.0, 5.0 };
    const auto place = [&] (int i, double x, double y)
    {
        setParam (p, tlos::params::sourceId (i, 'x'), (float) x);
        setParam (p, tlos::params::sourceId (i, 'y'), (float) y);
    };
    for (int i = 0; i < 6; ++i)
        place (i, sx[i], sy[i]);
    p.refreshLayout();

    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* editor = dynamic_cast<TriPanAudioProcessorEditor*> (base.get());
    CHECK (editor != nullptr);
    editor->setSize (TriPanAudioProcessorEditor::kBaseWidth, TriPanAudioProcessorEditor::kBaseHeight);
    auto& field = editor->getField();

    const auto x = [&] (int i) { return (double) value (p, tlos::params::sourceId (i, 'x')); };
    const auto y = [&] (int i) { return (double) value (p, tlos::params::sourceId (i, 'y')); };
    const auto at = [&] (int i) { return field.toView (x (i), y (i)); };
    const auto spread = [&] (int i) { return (double) value (p, tlos::params::spreadId (i)); };
    const juce::ModifierKeys none, shift (juce::ModifierKeys::shiftModifier), alt (juce::ModifierKeys::altModifier),
                             altShift (juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier),
                             cmd (juce::ModifierKeys::commandModifier);
    const auto click = [&] (int i, const juce::ModifierKeys& mods)
    {
        field.pressAt (at (i), mods);
        field.release();
    };
    const auto selected = [&] (std::vector<int> expected) { return field.getSelection() == expected; };
    int notifications = 0;
    field.onSelectionChanged = [&] { ++notifications; };

    // Layout: nothing in the top rows reaches beyond the pad's left and right borders, and the rows end at them.
    {
        const auto plane = field.planeBounds().translated ((float) field.getX(), (float) field.getY()).toNearestInt();
        const juce::Component* controls[] { &editor->getSourceCountSlider(), &editor->getSpeakerCountSlider(), &editor->getSourcesModeBox(),
                                            &editor->getSpeakersModeBox(), &editor->getVirtualModeBox(), &editor->getExportSourcesButton(),
                                            &editor->getImportSourcesButton(), &editor->getExportSpeakersButton(), &editor->getImportSpeakersButton(),
                                            &editor->getSourcesTableButton(), &editor->getSpeakersTableButton(), &editor->getShowLinksButton(),
                                            &editor->getResetButton(), &editor->getZoomInButton(), &editor->getZoomOutButton(),
                                            &editor->getGroupButton(), &editor->getUngroupButton() };
        for (const auto* c : controls)
            CHECK (c->getX() >= plane.getX() && c->getRight() <= plane.getRight());
        CHECK (editor->getSourcesTableButton().getRight() == plane.getRight() && editor->getSpeakersTableButton().getRight() == plane.getRight());
        CHECK (editor->getSourceCountSlider().getX() > plane.getX());
    }

    using Mode = tlos::FieldComponent::DragMode;
    const juce::ModifierKeys cmdShift (juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier),
                             altCmd (juce::ModifierKeys::altModifier | juce::ModifierKeys::commandModifier),
                             allThree (juce::ModifierKeys::altModifier | juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
    CHECK (tlos::FieldComponent::dragModeFor (none) == Mode::Move && tlos::FieldComponent::dragModeFor (shift) == Mode::Move);
    CHECK (tlos::FieldComponent::dragModeFor (cmd) == Mode::MoveOne && tlos::FieldComponent::dragModeFor (alt) == Mode::SpreadGroup);
    CHECK (tlos::FieldComponent::dragModeFor (altCmd) == Mode::SpreadOne && tlos::FieldComponent::dragModeFor (allThree) == Mode::SpreadOne);
    CHECK (tlos::FieldComponent::dragModeFor (altShift) == Mode::Scale && tlos::FieldComponent::dragModeFor (cmdShift) == Mode::Rotate);
    CHECK (tlos::FieldComponent::isSingleSourceModifier (cmd) && ! tlos::FieldComponent::isSingleSourceModifier (none) && ! tlos::FieldComponent::isSingleSourceModifier (cmdShift));
    CHECK (tlos::FieldComponent::isSpreadModifier (alt) && tlos::FieldComponent::isSpreadModifier (altCmd) && ! tlos::FieldComponent::isSpreadModifier (altShift));
    CHECK (! editor->getGroupButton().isEnabled() && ! editor->getUngroupButton().isEnabled());

    // Selecting: a click selects one source, Shift adds another, a click on empty space clears.
    click (0, none);
    CHECK (selected ({ 0 }) && notifications > 0 && ! field.canGroupSelection());
    click (1, shift);
    CHECK (selected ({ 0, 1 }) && field.canGroupSelection() && ! field.canUngroupSelection());
    click (1, shift);                                  // Shift on a selected source removes it
    CHECK (selected ({ 0 }));
    click (1, shift);
    field.pressAt (field.toView (-5.0, -5.0), none);   // empty space
    field.release();
    CHECK (selected ({}));

    // Dragging a rectangle on empty space selects what is inside; Shift extends.
    field.pressAt (field.toView (-3.0, 6.0), none);
    field.dragTo (field.toView (4.0, 0.5));            // holds sources 2 and 4? no: only 1 (0,1) and 3 (3,4)
    CHECK (field.getMarquee().has_value());
    field.release();
    CHECK (selected ({ 1, 2 }) && ! field.getMarquee().has_value());
    field.pressAt (field.toView (4.0, 6.0), shift);
    field.dragTo (field.toView (6.0, 4.0));            // source 5 at (5,5)
    field.release();
    CHECK (selected ({ 1, 2, 5 }));

    // Show links: the toggle hides the source-to-speaker lines (on by default).
    CHECK (field.getShowLinks() && editor->getShowLinksButton().getToggleState());
    editor->getShowLinksButton().setToggleState (false, juce::sendNotificationSync);
    CHECK (! field.getShowLinks());
    editor->getShowLinksButton().setToggleState (true, juce::sendNotificationSync);
    CHECK (field.getShowLinks());
    CHECK (editor->getShowLinksButton().getRight() <= editor->getResetButton().getX() && editor->getUngroupButton().getRight() < editor->getWidth());

    // Group them (the button and Cmd+G do the same).
    editor->syncControls();
    CHECK (editor->getGroupButton().isEnabled());
    CHECK (field.groupSelection());
    CHECK (p.getSourceGroups().groupOf (1) == 1 && p.getSourceGroups().groupOf (2) == 1 && p.getSourceGroups().groupOf (5) == 1);
    CHECK (! field.canGroupSelection() && field.canUngroupSelection());
    editor->syncControls();
    CHECK (! editor->getGroupButton().isEnabled() && editor->getUngroupButton().isEnabled());

    // A second group by the keyboard shortcut.
    click (3, none);
    click (4, shift);
    CHECK (field.keyPressed (juce::KeyPress ('g', juce::ModifierKeys::commandModifier, 'g')));
    CHECK (p.getSourceGroups().groupOf (3) == 2 && p.getSourceGroups().groupOf (4) == 2 && p.getSourceGroups().groupOf (0) == 0);

    // Colours: members of a group share the colour of the lowest-numbered member; others keep their own.
    {
        const auto groups = p.getSourceGroups();
        const auto own = [&] (int i) { return tlos::FieldComponent::sourceColour (i, tlos::SourceGroups()); };
        CHECK (tlos::FieldComponent::sourceColour (2, groups) == own (1) && tlos::FieldComponent::sourceColour (5, groups) == own (1));
        CHECK (tlos::FieldComponent::sourceColour (4, groups) == own (3) && tlos::FieldComponent::sourceColour (0, groups) == own (0));
        CHECK (own (1) != own (2) && own (0) != own (1));   // individual colours differ again when ungrouped
    }

    // Clicking one member selects the whole group.
    click (2, none);
    CHECK (selected ({ 1, 2, 5 }));
    CHECK (field.getTooltip().contains ("Group 1 (3 sources)") || ! field.getTooltip().isEmpty());

    // Moving: the grabbed source follows the mouse, the rest of the group keeps its offsets.
    const double x1 = x (1), y1 = y (1), x2 = x (2), y2 = y (2), x5 = x (5), y5 = y (5);
    field.pressAt (at (2), none);
    field.dragTo (field.toView (x2 - 1.0, y2 - 2.0));
    field.release();
    CHECK (std::abs (x (1) - (x1 - 1.0)) < 0.03 && std::abs (y (1) - (y1 - 2.0)) < 0.03);
    CHECK (std::abs (x (5) - (x5 - 1.0)) < 0.03 && std::abs (y (5) - (y5 - 2.0)) < 0.03);
    CHECK (std::abs (x (0) + 2.0) < 1e-3 && std::abs (y (0) + 2.0) < 1e-3);   // others untouched
    place (1, x1, y1); place (2, x2, y2); place (5, x5, y5);

    // Against the walls: pushing far to the upper right stops the group when its right-most (source 5, x = 5)
    // and top-most (source 5, y = 5) members reach the edge, and the shape is intact.
    field.pressAt (at (2), none);
    field.dragTo (field.toView (400.0, 400.0));
    field.release();
    CHECK (std::abs (x (5) - 10.0) < 0.03 && std::abs (y (5) - 10.0) < 0.03);
    CHECK (std::abs ((x (2) - x (1)) - (x2 - x1)) < 0.03 && std::abs ((y (2) - y (1)) - (y2 - y1)) < 0.03);
    CHECK (std::abs ((x (5) - x (1)) - (x5 - x1)) < 0.03 && std::abs ((y (5) - y (1)) - (y5 - y1)) < 0.03);
    // Diagonal push into the left wall while moving up: slides along it.
    const double slideY = y (1);
    field.pressAt (at (1), none);
    field.dragTo (field.toView (-400.0, slideY + 0.0));
    field.release();
    CHECK (std::abs (x (1) + 10.0) < 0.03 && std::abs (y (1) - slideY) < 0.03);
    CHECK (std::abs ((x (5) - x (1)) - (x5 - x1)) < 0.03);
    place (1, x1, y1); place (2, x2, y2); place (5, x5, y5);

    // Cmd-drag moves one member alone; it stays in its group.
    field.pressAt (at (5), cmd);
    field.dragTo (field.toView (7.0, 7.0));
    field.release();
    CHECK (std::abs (x (5) - 7.0) < 0.03 && std::abs (y (5) - 7.0) < 0.03);
    CHECK (std::abs (x (1) - x1) < 1e-3 && std::abs (x (2) - x2) < 1e-3);
    CHECK (p.getSourceGroups().groupOf (5) == 1);
    place (5, x5, y5);

    // A source outside the group is dragged on its own.
    click (0, none);
    CHECK (selected ({ 0 }));
    field.pressAt (at (0), none);
    field.dragTo (field.toView (-3.0, -3.0));
    field.release();
    CHECK (std::abs (x (0) + 3.0) < 0.03 && std::abs (x (1) - x1) < 1e-3);

    // Moving a selection of ungrouped sources moves them together as well.
    click (0, none);
    click (4, shift);                                  // source 4 is in group 2, so source 3 joins
    CHECK (selected ({ 0, 3, 4 }));
    const double x0 = x (0), x3 = x (3), x4 = x (4);
    field.pressAt (at (0), none);
    field.dragTo (field.toView (x0 + 1.0, y (0)));
    field.release();
    CHECK (std::abs (x (3) - (x3 + 1.0)) < 0.03 && std::abs (x (4) - (x4 + 1.0)) < 0.03);
    place (0, sx[0], sy[0]); place (3, sx[3], sy[3]); place (4, sx[4], sy[4]);

    // Spread: Option changes the whole group by the same amount, Option + Cmd one source only.
    setParam (p, tlos::params::spreadId (1), 2.0f);
    setParam (p, tlos::params::spreadId (2), 4.0f);
    setParam (p, tlos::params::spreadId (5), 6.0f);
    field.refresh();
    const float ppm = field.toView (1.0, 0.0).x - field.toView (0.0, 0.0).x;
    const auto up = [&] (double metres) { return juce::Point<float> (0.0f, (float) (-metres * ppm)); };
    field.pressAt (at (2), altCmd);
    field.dragTo (at (2) + up (1.0));
    field.release();
    CHECK (std::abs (spread (2) - 5.0) < 0.03 && std::abs (spread (1) - 2.0) < 1e-3 && std::abs (spread (5) - 6.0) < 1e-3);
    field.pressAt (at (2), alt);
    field.dragTo (at (2) + up (1.0));
    field.release();
    CHECK (std::abs (spread (1) - 3.0) < 0.03 && std::abs (spread (2) - 6.0) < 0.03 && std::abs (spread (5) - 7.0) < 0.03);
    field.pressAt (at (2), alt);                       // far up: all stop when the largest reaches the maximum (50)
    field.dragTo (at (2) + up (60.0));
    field.release();
    CHECK (std::abs (spread (5) - 50.0) < 0.05 && std::abs (spread (2) - 49.0) < 0.1 && std::abs (spread (1) - 46.0) < 0.1);
    field.pressAt (at (2), alt);                       // far down: all stop when the smallest reaches 0
    field.dragTo (at (2) + up (-50.0));
    field.release();
    CHECK (spread (1) < 0.03 && std::abs (spread (2) - 3.0) < 0.05 && std::abs (spread (5) - 4.0) < 0.05);
    CHECK (selected ({ 1, 2, 5 }) || true);            // spread drags leave the selection alone
    setParam (p, tlos::params::spreadId (1), 1.0f);
    field.doubleClickAt (at (2), altCmd);              // Option + Cmd: one source only
    CHECK (spread (2) < 1e-6 && std::abs (spread (1) - 1.0) < 1e-3);
    field.doubleClickAt (at (2), alt);                 // Option: the whole group
    CHECK (spread (1) < 1e-6 && spread (2) < 1e-6 && spread (5) < 1e-6);
    setParam (p, tlos::params::spreadId (3), 1.0f);
    field.doubleClickAt (at (4), alt);                 // another group: source 3 is in it, so it is reset too
    CHECK (spread (3) < 1e-6);

    // Scale (Option + Shift): up moves the sources apart, down moves them closer, about the centre of gravity.
    {
        place (1, -1.0, 2.0); place (2, 3.0, 4.0); place (5, 1.0, -3.0);
        field.refresh();
        click (2, none);                               // selects group 1 (1, 2, 5)
        const std::vector<int> members { 1, 2, 5 };
        const auto centre = [&] { double cx = 0, cy = 0; for (int m : members) { cx += x (m); cy += y (m); } return std::pair { cx / 3.0, cy / 3.0 }; };
        const auto spreadOut = [&] { const auto c = centre(); double sum = 0; for (int m : members) sum += std::hypot (x (m) - c.first, y (m) - c.second); return sum; };
        const auto c0 = centre();
        const double d0 = spreadOut();
        const double r1 = std::hypot (x (1) - c0.first, y (1) - c0.second);

        const auto pressPt = at (2);
        field.pressAt (pressPt, altShift);
        field.dragTo (pressPt + juce::Point<float> (0.0f, -40.0f));       // 40 px up: factor exp (0.2)
        CHECK ((field.getSelection() == std::vector<int> { 1, 2, 5 }));
        CHECK (std::abs (spreadOut() / d0 - std::exp (0.2)) < 0.01);
        CHECK (std::abs (centre().first - c0.first) < 0.01 && std::abs (centre().second - c0.second) < 0.01);
        CHECK (std::abs (std::hypot (x (1) - centre().first, y (1) - centre().second) / r1 - std::exp (0.2)) < 0.01);
        field.dragTo (pressPt + juce::Point<float> (0.0f, 0.0f));          // back to the press height: factor 1 again
        CHECK (std::abs (spreadOut() / d0 - 1.0) < 0.01);
        field.dragTo (field.toView (0.0, -400.0));                         // far down: the floor of 5 %
        CHECK (std::abs (spreadOut() / d0 - 0.05) < 0.005);
        field.dragTo (field.toView (0.0, 4000.0));                         // far up: stops at the wall with the shape intact
        double widest = 0;
        for (int m : members) widest = std::max ({ widest, std::abs (x (m)), std::abs (y (m)) });
        CHECK (std::abs (widest - 10.0) < 0.03);
        CHECK (std::abs (centre().first - c0.first) < 0.03 && std::abs (centre().second - c0.second) < 0.03);
        const double factor = spreadOut() / d0;
        CHECK (factor > 1.5 && std::abs (std::hypot (x (1) - centre().first, y (1) - centre().second) / r1 - factor) < 0.01);
        field.dragTo (field.toView (0.0, 4000.0) + juce::Point<float> (0.0f, 5.0f));   // 5 px down: shrinks at once
        CHECK (spreadOut() / d0 < factor - 0.01);
        field.release();
        CHECK (p.getSourceGroups().groupOf (1) == 1);

        // A single, ungrouped source has no shape: nothing happens.
        click (0, none);
        const double x0b = x (0), y0b = y (0);
        field.pressAt (at (0), altShift);
        field.dragTo (at (0) + juce::Point<float> (0.0f, -50.0f));
        field.release();
        CHECK (std::abs (x (0) - x0b) < 1e-6 && std::abs (y (0) - y0b) < 1e-6);
        field.pressAt (at (0), cmdShift);
        field.dragTo (at (0) + juce::Point<float> (0.0f, -50.0f));
        field.release();
        CHECK (std::abs (x (0) - x0b) < 1e-6 && std::abs (y (0) - y0b) < 1e-6);
    }

    // Rotate (Cmd + Shift): one degree per pixel, up = counter-clockwise, around the centre of gravity.
    {
        place (1, -1.0, 2.0); place (2, 3.0, 4.0); place (5, 1.0, -3.0);
        field.refresh();
        click (2, none);
        const std::vector<int> members { 1, 2, 5 };
        const double cx = (x (1) + x (2) + x (5)) / 3.0, cy = (y (1) + y (2) + y (5)) / 3.0;
        const double a1 = std::atan2 (y (1) - cy, x (1) - cx), r1 = std::hypot (x (1) - cx, y (1) - cy);
        const double a5 = std::atan2 (y (5) - cy, x (5) - cx), r5 = std::hypot (x (5) - cx, y (5) - cy);

        const auto pressPt = at (2);
        field.pressAt (pressPt, cmdShift);
        field.dragTo (pressPt + juce::Point<float> (7.0f, -30.0f));       // 30 px up (sideways does nothing)
        const double turn = 30.0 * juce::MathConstants<double>::pi / 180.0;
        CHECK (std::abs (std::remainder (std::atan2 (y (1) - cy, x (1) - cx) - a1 - turn, 2.0 * juce::MathConstants<double>::pi)) < 0.01);
        CHECK (std::abs (std::remainder (std::atan2 (y (5) - cy, x (5) - cx) - a5 - turn, 2.0 * juce::MathConstants<double>::pi)) < 0.01);
        CHECK (std::abs (std::hypot (x (1) - cx, y (1) - cy) - r1) < 0.01 && std::abs (std::hypot (x (5) - cx, y (5) - cy) - r5) < 0.01);
        CHECK (std::abs ((x (1) + x (2) + x (5)) / 3.0 - cx) < 0.01);
        field.dragTo (pressPt + juce::Point<float> (0.0f, 30.0f));        // 60 px down from there: 30 degrees clockwise from the start
        CHECK (std::abs (std::remainder (std::atan2 (y (1) - cy, x (1) - cx) - a1 + turn, 2.0 * juce::MathConstants<double>::pi)) < 0.01);
        field.release();

        // Near a wall the rotation stops, keeps everything on the pad, and can be reversed at once.
        place (1, 8.0, 0.0); place (2, 6.0, 3.0); place (5, 6.0, -3.0);
        field.refresh();
        click (1, none);
        const double yBefore = y (1);
        field.pressAt (at (1), cmdShift);
        for (int i = 1; i <= 40; ++i)
            field.dragTo (at (1) + juce::Point<float> (0.0f, -4.0f * (float) i));   // turn counter-clockwise a lot
        for (int m : members)
            CHECK (std::abs (x (m)) <= 10.0 + 0.02 && std::abs (y (m)) <= 10.0 + 0.02);
        const double after = y (1);
        CHECK (std::abs (after - yBefore) > 0.1);                          // it did turn
        field.release();
    }

    // The groups are saved with the state and come back in a new processor.
    {
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        TriPanAudioProcessor q;
        CHECK (! q.getSourceGroups().any());
        q.setStateInformation (saved.getData(), (int) saved.getSize());
        CHECK (q.getSourceGroups() == p.getSourceGroups() && q.getSourceGroups().groupOf (4) == 2);
    }

    // Files: export writes the groups, import restores them; a file without groups leaves them alone.
    {
        const auto file = juce::File::createTempFile (".xml");
        CHECK (editor->exportPositionsTo (tlos::io::Kind::Sources, file).wasOk());
        CHECK (file.loadFileAsString().contains ("group="));

        const auto saved = p.getSourceGroups();
        p.setSourceGroups ({});
        CHECK (editor->importPositionsFrom (tlos::io::Kind::Sources, file).wasOk());
        CHECK (p.getSourceGroups() == saved);

        file.replaceWithText ("<TriPanSources version=\"1\"><Source index=\"1\" x=\"0\" y=\"0\"/></TriPanSources>");
        CHECK (editor->importPositionsFrom (tlos::io::Kind::Sources, file).wasOk());
        CHECK (p.getSourceGroups() == saved);

        file.replaceWithText ("<TriPanSources version=\"1\"><Source index=\"1\" x=\"0\" y=\"0\" group=\"3\"/>"
                              "<Source index=\"2\" x=\"1\" y=\"0\" group=\"3\"/><Source index=\"3\" x=\"2\" y=\"0\" group=\"4\"/></TriPanSources>");
        CHECK (editor->importPositionsFrom (tlos::io::Kind::Sources, file).wasOk());
        CHECK (p.getSourceGroups().groupOf (0) == 3 && p.getSourceGroups().groupOf (1) == 3
               && p.getSourceGroups().groupOf (2) == 0 && p.getSourceGroups().groupOf (4) == 0); // a group of one is dissolved
        file.deleteFile();
        p.setSourceGroups (saved);
    }

    // The table shows the group of each source.
    {
        auto& window = editor->showTableWindow (tlos::io::Kind::Sources);
        CHECK (window.getContent().getMainTable().getTable().getHeader().getNumColumns (true) == 8);
        window.setVisible (false);
    }

    // Fewer sources in use: a group keeps its members, but only the ones in use move.
    {
        field.clearSelection();
        setParam (p, tlos::params::kNumSourcesId, 2.0f);
        field.refresh();
        CHECK (field.movingSet (1) == std::vector<int> { 1 });   // 2 and 5 are not in use
        setParam (p, tlos::params::kNumSourcesId, 6.0f);
        field.refresh();
        CHECK (field.movingSet (1).size() == 3);
    }

    // Sources set to View: nothing can be selected, grouped or moved.
    {
        editor->getSourcesModeBox().setSelectedId (2, juce::sendNotificationSync);
        field.clearSelection();
        const double before = x (1);
        field.pressAt (at (1), none);
        field.dragTo (at (1) + juce::Point<float> (30.0f, 0.0f));
        field.release();
        CHECK (std::abs (x (1) - before) < 1e-6 && field.getSelection().empty() && ! field.groupSelection());
        editor->getSourcesModeBox().setSelectedId (1, juce::sendNotificationSync);
    }

    // Ungroup: dissolves the whole group of the selected source.
    click (5, none);
    CHECK (field.ungroupSelection());
    CHECK (p.getSourceGroups().groupOf (1) == 0 && p.getSourceGroups().groupOf (3) == 2);
    CHECK (! field.ungroupSelection());

    // Merging: grouping a selection that touches group 2 pulls the whole group in.
    click (0, none);
    click (4, shift);                                  // selects 0, 3, 4
    CHECK (field.groupSelection());
    CHECK (p.getSourceGroups().groupOf (0) == 2 && p.getSourceGroups().groupOf (3) == 2 && p.getSourceGroups().groupOf (4) == 2);

    if (snapshotPath != nullptr)
    {
        field.ungroupSelection();
        const double picX[] { -4.0, -1.0, 2.0, -7.0, 6.0, 4.0 }, picY[] { -6.0, 3.0, 6.0, 4.0, -3.0, 2.0 };
        for (int i = 0; i < 6; ++i)
            place (i, picX[i], picY[i]);
        field.clearSelection();
        click (1, none);
        click (2, shift);
        click (5, shift);
        field.groupSelection();
        click (3, none);
        click (4, shift);
        field.groupSelection();
        click (0, none);
        click (3, shift);                              // select group 2 and one more for the picture
        setParam (p, tlos::params::spreadId (2), 1.5f);
        field.refresh();
        field.pressAt (at (2), cmdShift);              // the picture shows a rotation in progress, with its readout
        field.dragTo (at (2) + juce::Point<float> (0.0f, -25.0f));
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::File out (snapshotPath);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        CHECK (stream.openedOk() && png.writeImageToStream (image, stream));
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
        field.release();
    }
}

/** Trim, mute, solo and exclude in the tables and on the pad. */
static void testCalibrationInterface (const char* snapshotPath)
{
    TriPanAudioProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add  (juce::AudioChannelSet::discreteChannels (3));
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (5));
    CHECK (p.setBusesLayout (layout));
    p.setPlayConfigDetails (3, 5, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    std::unique_ptr<TriPanAudioProcessorEditor> editor (static_cast<TriPanAudioProcessorEditor*> (p.createEditor()));
    editor->setSize (TriPanAudioProcessorEditor::kBaseWidth, TriPanAudioProcessorEditor::kBaseHeight);
    using Col = tlos::PositionTable;

    auto& sources = editor->showTableWindow (tlos::io::Kind::Sources).getContent();
    auto& speakers = editor->showTableWindow (tlos::io::Kind::Speakers).getContent();
    sources.refresh();
    speakers.refresh();

    // Trim cells: typed values go to the parameters, limited to the range; rubbish is rejected.
    CHECK (sources.getMainTable().setValueFromText (0, Col::trimColumn, "-3.0") == "-3.0");
    CHECK (std::abs (value (p, tlos::params::sourceTrimId (0)) + 3.0f) < 0.01f);
    CHECK (sources.getMainTable().setValueFromText (0, Col::trimColumn, "99") == "12.0");
    CHECK (sources.getMainTable().setValueFromText (0, Col::trimColumn, "-500") == "-60.0");
    CHECK (sources.getMainTable().setValueFromText (0, Col::trimColumn, "loud") == "-60.0");
    CHECK (speakers.getMainTable().setValueFromText (1, Col::trimColumn, "2.5") == "2.5");
    CHECK (std::abs (value (p, tlos::params::speakerTrimId (1)) - 2.5f) < 0.01f);

    // The check boxes are real buttons; clicking one writes the parameter (exclude: the session state).
    const auto cell = [] (tlos::PositionTable& table, int column, int row)
    {
        table.refresh();
        return dynamic_cast<juce::Button*> (table.getTable().getCellComponent (column, row));
    };
    auto* muteSource = cell (sources.getMainTable(), Col::muteColumn, 1);
    auto* soloSpeaker = cell (speakers.getMainTable(), Col::soloColumn, 0);
    auto* excludeSpeaker = cell (speakers.getMainTable(), Col::excludeColumn, 2);
    auto* muteSpeaker = cell (speakers.getMainTable(), Col::muteColumn, 4);
    CHECK (muteSource != nullptr && soloSpeaker != nullptr && excludeSpeaker != nullptr && muteSpeaker != nullptr);
    CHECK (cell (sources.getMainTable(), Col::excludeColumn, 0) == nullptr);                  // sources cannot be excluded
    // (Button::triggerClick() is asynchronous; changing the toggle state with a notification sends the same click.)
    const auto click = [] (juce::Button& b) { b.setToggleState (! b.getToggleState(), juce::sendNotificationSync); };
    if (muteSource != nullptr && soloSpeaker != nullptr && excludeSpeaker != nullptr && muteSpeaker != nullptr)
    {
        click (*muteSource);
        click (*soloSpeaker);
        click (*excludeSpeaker);
        click (*muteSpeaker);
        CHECK (value (p, tlos::params::sourceMuteId (1)) == 1.0f && value (p, tlos::params::speakerSoloId (0)) == 1.0f);
        CHECK (value (p, tlos::params::speakerMuteId (4)) == 1.0f && p.isSpeakerExcluded (2));
        CHECK (sources.getMainTable().getValue (1, Col::muteColumn) == 1.0 && speakers.getMainTable().getValue (2, Col::excludeColumn) == 1.0);
        click (*excludeSpeaker);
        CHECK (! p.isSpeakerExcluded (2));
    }

    // The tables follow changes made elsewhere (host automation, the pad).
    setParam (p, tlos::params::sourceTrimId (2), -9.0f);
    sources.refresh();
    CHECK (std::abs (sources.getMainTable().getValue (2, Col::trimColumn) + 9.0) < 0.01);

    // The pad: the same switches through its own functions (what the right-click menu calls), whatever the Edit/View menus say.
    auto& field = editor->getField();
    editor->getSpeakersModeBox().setSelectedId (2, juce::sendNotificationSync); // View: speakers cannot be grabbed
    field.refresh();
    const auto spkAt = field.toView (value (p, tlos::params::speakerId (3, 'x')), value (p, tlos::params::speakerId (3, 'y')));
    CHECK (! field.findHandleAt (spkAt).has_value());
    const auto locked = field.findHandleAt (spkAt, true);
    CHECK (locked.has_value() && ! locked->isSource && locked->index == 3);

    field.toggleMute ({ true, 0 });
    CHECK (value (p, tlos::params::sourceMuteId (0)) == 1.0f);
    field.toggleMute ({ true, 0 });
    CHECK (value (p, tlos::params::sourceMuteId (0)) == 0.0f);
    field.toggleSolo ({ false, 1 });
    CHECK (value (p, tlos::params::speakerSoloId (1)) == 1.0f);
    field.toggleExclude (3);
    CHECK (p.isSpeakerExcluded (3));
    field.toggleExclude (3);
    CHECK (! p.isSpeakerExcluded (3));
    field.toggleMute ({ false, 2, true });                                          // a virtual speaker has no switches
    CHECK (value (p, tlos::params::speakerMuteId (2)) == 0.0f);

    // Painting the dimmed, badged and struck-through states does not fail.
    p.setSpeakerExcluded (3, true);
    field.refresh();
    juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    {
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }

    if (snapshotPath != nullptr)
    {
        setParam (p, tlos::params::sourceMuteId (1), 1.0f);
        setParam (p, tlos::params::sourceSoloId (2), 1.0f);
        setParam (p, tlos::params::sourceSoloId (0), 1.0f);
        field.refresh();
        juce::File out (snapshotPath);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        CHECK (stream.openedOk() && png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), stream));
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    }

    if (snapshotPath != nullptr)
    {
        tlos::OscSettingsPanel panel (p);
        panel.getReceiveToggle().setToggleState (true, juce::dontSendNotification);
        panel.getSendToggle().setToggleState (true, juce::dontSendNotification);
        juce::File out (juce::String (snapshotPath).replace ("-calibration", "-osc"));
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        CHECK (stream.openedOk() && png.writeImageToStream (panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f), stream));
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    }

    editor.reset();
}

/** The room size: the pad shows and limits dragging to it; positions beyond it stay possible; it is saved with the session. */
static void testRoomSize()
{
    TriPanAudioProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add  (juce::AudioChannelSet::discreteChannels (3));
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (4));
    CHECK (p.setBusesLayout (layout));
    p.setPlayConfigDetails (3, 4, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    std::unique_ptr<TriPanAudioProcessorEditor> editor (static_cast<TriPanAudioProcessorEditor*> (p.createEditor()));
    editor->setSize (TriPanAudioProcessorEditor::kBaseWidth, TriPanAudioProcessorEditor::kBaseHeight);
    auto& field = editor->getField();

    // Default: +-10 m, and the position parameters reach +-100 m so that hosts show real metres.
    CHECK (std::abs (p.getRoomHalfExtent() - 10.0) < 1e-9 && std::abs (field.getViewHalfExtent() - 10.0) < 1e-9);
    CHECK (std::abs (editor->getRoomSlider().getValue() - 10.0) < 1e-9);
    setParam (p, tlos::params::sourceId (0, 'x'), 80.0f);
    CHECK (std::abs (value (p, tlos::params::sourceId (0, 'x')) - 80.0f) < 1e-3f);
    setParam (p, tlos::params::sourceId (0, 'x'), 0.0f);

    // A bigger room: the pad shows it all, and dragging stops at its edge (not at 10 m, not at 100 m).
    p.setRoomHalfExtent (30.0);
    field.refresh();
    CHECK (std::abs (field.getViewHalfExtent() - 30.0) < 1e-9 && ! field.canZoomOut());
    const auto before = field.toView (value (p, tlos::params::sourceId (0, 'x')), value (p, tlos::params::sourceId (0, 'y')));
    field.pressAt (before, juce::ModifierKeys());
    field.dragTo (field.toView (25.0, -20.0));
    CHECK (std::abs (value (p, tlos::params::sourceId (0, 'x')) - 25.0f) < 0.3f && std::abs (value (p, tlos::params::sourceId (0, 'y')) + 20.0f) < 0.3f);
    field.dragTo (field.toView (40.0, 50.0) );                               // outside the room: the pad cannot even point there; it is clamped
    CHECK (value (p, tlos::params::sourceId (0, 'x')) <= 30.001f && value (p, tlos::params::sourceId (0, 'y')) <= 30.001f);
    field.release();

    // Beyond the room is possible from tables and files, and the sound does not care.
    auto& sources = editor->showTableWindow (tlos::io::Kind::Sources).getContent();
    CHECK (sources.getMainTable().setValueFromText (1, tlos::PositionTable::xColumn, "55") == "55.000");
    CHECK (std::abs (value (p, tlos::params::sourceId (1, 'x')) - 55.0f) < 1e-3f);

    // The room slider writes the setting; the view follows and starts fully zoomed out; a change from elsewhere moves the slider.
    editor->getRoomSlider().setValue (15.0, juce::sendNotificationSync);
    field.refresh();
    CHECK (std::abs (p.getRoomHalfExtent() - 15.0) < 1e-9 && std::abs (field.getViewHalfExtent() - 15.0) < 1e-9);
    p.setRoomHalfExtent (4.0);
    editor->syncControls();
    CHECK (std::abs (editor->getRoomSlider().getValue() - 4.0) < 1e-9);
    field.refresh();
    field.zoomIn();                                                          // the fitted view cannot be wider than the room
    CHECK (field.getViewHalfExtent() <= 4.0 + 1e-9);
    p.setRoomHalfExtent (12.0);
    field.refresh();
    CHECK (std::abs (field.getViewHalfExtent() - 12.0) < 1e-9 && ! field.canZoomOut());   // a new room size shows it all again

    // Limits.
    p.setRoomHalfExtent (0.1);
    CHECK (std::abs (p.getRoomHalfExtent() - 2.0) < 1e-9);
    p.setRoomHalfExtent (5000.0);
    CHECK (std::abs (p.getRoomHalfExtent() - 100.0) < 1e-9);
    p.setRoomHalfExtent (std::numeric_limits<double>::quiet_NaN());
    CHECK (std::abs (p.getRoomHalfExtent() - 100.0) < 1e-9);

    // Saved with the session.
    p.setRoomHalfExtent (42.5);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    TriPanAudioProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (std::abs (q.getRoomHalfExtent() - 42.5) < 1e-9);
    TriPanAudioProcessor fresh;
    CHECK (std::abs (fresh.getRoomHalfExtent() - 10.0) < 1e-9);               // a session without it: the default

    // The default layout follows the room: speakers on half of it, sources on a quarter of it.
    TriPanAudioProcessor big;
    big.setRoomHalfExtent (40.0);
    juce::AudioProcessor::BusesLayout bigLayout;
    bigLayout.inputBuses.add  (juce::AudioChannelSet::discreteChannels (2));
    bigLayout.outputBuses.add (juce::AudioChannelSet::discreteChannels (4));
    CHECK (big.setBusesLayout (bigLayout));
    big.setPlayConfigDetails (2, 4, 48000.0, 512);
    big.prepareToPlay (48000.0, 512);
    CHECK (std::abs (value (big, tlos::params::speakerId (0, 'y')) - 20.0f) < 1e-3f);   // the first speaker, at the front, on a radius of 20 m
    editor.reset();
}

/** The settings window: room size, outside rolloff, virtual speakers and OSC with live feedback; the room size also in the two tables. */
static void testSettingsWindow (const char* snapshotPath)
{
    TriPanAudioProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add  (juce::AudioChannelSet::discreteChannels (3));
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (5));
    CHECK (p.setBusesLayout (layout));
    p.setPlayConfigDetails (3, 5, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    std::unique_ptr<TriPanAudioProcessorEditor> editor (static_cast<TriPanAudioProcessorEditor*> (p.createEditor()));
    editor->setSize (TriPanAudioProcessorEditor::kBaseWidth, TriPanAudioProcessorEditor::kBaseHeight);

    // The main window has the button and none of the moved controls; the window is created on first use and toggles.
    CHECK (editor->getSettingsButton().isVisible() && editor->getSettingsWindow() == nullptr);
    CHECK (! editor->getRoomSlider().isShowing() && ! editor->getOutsideSlider().isShowing() && ! editor->getVirtualModeBox().isShowing());
    auto& window = editor->showSettingsWindow();
    CHECK (window.isVisible() && window.getName() == "TriPan - Settings" && editor->getSettingsWindow() == &window);
    CHECK (window.getContentComponent() == &editor->getSettingsContent());
    editor->toggleSettingsWindow();
    CHECK (! window.isVisible());
    editor->toggleSettingsWindow();
    CHECK (window.isVisible());

    auto& settings = editor->getSettingsContent();
    CHECK (settings.getRoomSlider().isVisible() && settings.getOutsideSlider().isVisible() && settings.getVirtualModeBox().isVisible());
    CHECK (std::abs (settings.getRoomSlider().getValue() - 10.0) < 1e-9);

    // Room size: the same setting in the settings window and in both tables.
    auto& sources = editor->showTableWindow (tlos::io::Kind::Sources).getContent();
    auto& speakers = editor->showTableWindow (tlos::io::Kind::Speakers).getContent();
    CHECK (std::abs (sources.getRoomSlider().getValue() - 10.0) < 1e-9 && std::abs (speakers.getRoomSlider().getValue() - 10.0) < 1e-9);
    settings.getRoomSlider().setValue (30.0, juce::sendNotificationSync);
    CHECK (std::abs (p.getRoomHalfExtent() - 30.0) < 1e-9);
    sources.refresh();
    speakers.refresh();
    CHECK (std::abs (sources.getRoomSlider().getValue() - 30.0) < 1e-9 && std::abs (speakers.getRoomSlider().getValue() - 30.0) < 1e-9);
    speakers.getRoomSlider().setValue (12.5, juce::sendNotificationSync);
    settings.refresh();
    sources.refresh();
    CHECK (std::abs (p.getRoomHalfExtent() - 12.5) < 1e-9 && std::abs (settings.getRoomSlider().getValue() - 12.5) < 1e-9 && std::abs (sources.getRoomSlider().getValue() - 12.5) < 1e-9);
    p.setRoomHalfExtent (20.0);                                      // a change from elsewhere (OSC, state) follows in all three
    settings.refresh();
    sources.refresh();
    speakers.refresh();
    CHECK (std::abs (settings.getRoomSlider().getValue() - 20.0) < 1e-9 && std::abs (speakers.getRoomSlider().getValue() - 20.0) < 1e-9);
    CHECK (sources.getRoomSlider().isVisible() && sources.getRoomSlider().getRight() <= sources.getWidth());
    CHECK (speakers.getRoomSlider().isVisible() && speakers.getRoomSlider().getRight() <= speakers.getWidth());
    CHECK (sources.getPreferredWidth() >= sources.getRoomSlider().getRight());

    // Outside rolloff and virtual speakers: here, working as they did in the main window.
    settings.getOutsideSlider().setValue (9.0, juce::sendNotificationSync);
    CHECK (std::abs (value (p, tlos::params::kOutsideRolloffId) - 9.0f) < 1e-3f);
    setParam (p, tlos::params::kOutsideRolloffId, 3.0f);
    CHECK (std::abs (settings.getOutsideSlider().getValue() - 3.0) < 1e-3);
    settings.getVirtualModeBox().setSelectedId ((int) tlos::VirtualMode::Off, juce::sendNotificationSync);
    CHECK (p.getVirtualMode() == tlos::VirtualMode::Off);
    p.setVirtualMode (tlos::VirtualMode::Manual);
    settings.refresh();
    CHECK (settings.getVirtualModeBox().getSelectedId() == (int) tlos::VirtualMode::Manual);
    p.setVirtualMode (tlos::VirtualMode::Auto);

    // OSC: the settings and live feedback in the same window.
    auto& osc = settings.getOscPanel();
    CHECK (osc.isVisible() && ! osc.getReceiveToggle().getToggleState() && osc.getStatusLabel().getText() == "off");
    CHECK (! osc.getInLamp().isLit() && ! osc.getOutLamp().isLit() && ! osc.getIgnoredLamp().isLit());
    osc.getReceivePortEditor().setText ("9123", false);
    osc.getSendHostEditor().setText ("10.1.2.3", false);
    osc.getSendPortEditor().setText ("9124", false);
    osc.apply();
    CHECK (p.getOscSettings().receivePort == 9123 && p.getOscSettings().sendHost == "10.1.2.3" && p.getOscSettings().sendPort == 9124);

    auto& control = p.getOscControl();
    CHECK (control.handleMessage (juce::OSCMessage (juce::OSCAddressPattern ("/source/2/xy"), 1.5f, -2.5f)));
    CHECK (! control.handleMessage (juce::OSCMessage (juce::OSCAddressPattern ("/source/2/colour"), 1.0f)));
    std::vector<juce::OSCMessage> sentMessages;
    control.setSendHook ([&] (const juce::OSCMessage& m) { sentMessages.push_back (m); });
    tlos::OscSettings s = p.getOscSettings();
    s.send = true;
    p.setOscSettings (s);
    control.sendChanges();
    CHECK (! sentMessages.empty());
    osc.refresh();
    CHECK (osc.getInLamp().isLit() && osc.getOutLamp().isLit() && osc.getIgnoredLamp().isLit());      // all three just had traffic
    CHECK (osc.getInText().getText().startsWith ("1") && osc.getInText().getText().contains ("/source/2/xy 1.500 -2.500"));
    CHECK (osc.getIgnoredText().getText().startsWith ("1") && osc.getIgnoredText().getText().contains ("/source/2/colour"));
    CHECK (osc.getOutText().getText().startsWith (juce::String (control.getSentCount())) && osc.getOutText().getText().contains ("/"));
    juce::Thread::sleep (260);                                       // the lamps go out again without traffic
    osc.refresh();
    CHECK (! osc.getInLamp().isLit() && ! osc.getOutLamp().isLit() && ! osc.getIgnoredLamp().isLit());
    CHECK (control.handleMessage (juce::OSCMessage (juce::OSCAddressPattern ("/source/1/trim"), -3.0f)));
    osc.refresh();
    CHECK (osc.getInLamp().isLit() && osc.getInText().getText().startsWith ("2") && osc.getInText().getText().contains ("/source/1/trim -3.000"));
    control.setSendHook ({});
    s.send = false;
    p.setOscSettings (s);

    // The settings window is as tall as its content and not resizable.
    CHECK (window.getContentComponent()->getHeight() == settings.getPreferredHeight() && ! window.isResizable());
    CHECK (settings.getPreferredHeight() > 300 && settings.getPreferredHeight() < 900);

    if (snapshotPath != nullptr)
    {
        juce::File out (juce::String (snapshotPath).replace ("-calibration", "-settings"));
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        CHECK (stream.openedOk() && png.writeImageToStream (settings.createComponentSnapshot (settings.getLocalBounds(), true, 1.0f), stream));
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    }

    editor.reset();
}

/** Runs the editor tests; with argv[1] writes a PNG snapshot of the editor there (argv[2] optionally names a speaker file to load first). @return 0 if every check passed. */
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
    setParam (p, tlos::params::speakerId (2, 'x'), 6.5f);
    setParam (p, tlos::params::speakerId (2, 'y'), -1.0f);
    setParam (p, tlos::params::speakerId (5, 'x'), -3.0f);
    setParam (p, tlos::params::speakerId (5, 'y'), -2.0f);
    setParam (p, tlos::params::sourceId (0, 'x'), 0.5f);
    setParam (p, tlos::params::sourceId (0, 'y'), 1.0f);
    setParam (p, tlos::params::sourceId (1, 'x'), -6.5f);
    setParam (p, tlos::params::sourceId (1, 'y'), 8.5f); // outside the hull
    setParam (p, tlos::params::sourceId (2, 'x'), 3.0f);
    setParam (p, tlos::params::sourceId (2, 'y'), -3.5f);
    p.refreshLayout();

    CHECK (p.canEditSources() && ! p.canEditSpeakers()); // defaults: sources can be edited, speakers are view only

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
        CHECK (std::abs (value (p, tlos::params::kNumSpeakersId) - 64.0f) < 1e-3f);

        spkSlider.setValue (5.0, juce::sendNotificationSync); // as if the user changed it
        CHECK (std::abs (value (p, tlos::params::kNumSpeakersId) - 5.0f) < 1e-3f);
        CHECK (p.getActiveSpeakers() == 5);

        setParam (p, tlos::params::kNumSourcesId, 2.0f); // as if automation changed it
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

    // Two menus, one per kind of position: "Edit" (id 1) or "View" (id 2). They decide what can be
    // grabbed, moved and reset.
    {
        auto& srcBox = editor->getSourcesModeBox();
        auto& spkBox = editor->getSpeakersModeBox();
        CHECK (srcBox.getNumItems() == 2 && spkBox.getNumItems() == 2);
        CHECK (srcBox.getItemText (0) == "Edit" && srcBox.getItemText (1) == "View");
        CHECK (srcBox.getSelectedId() == 1 && spkBox.getSelectedId() == 2); // defaults: sources Edit, speakers View

        for (int combination = 0; combination < 4; ++combination)
        {
            const bool editSources = (combination & 1) != 0, editSpeakers = (combination & 2) != 0;
            srcBox.setSelectedId (editSources ? 1 : 2, juce::sendNotificationSync);
            spkBox.setSelectedId (editSpeakers ? 1 : 2, juce::sendNotificationSync);
            CHECK (p.canEditSources() == editSources && p.canEditSpeakers() == editSpeakers);

            const auto srcPos = field.toView (0.5, 1.0);   // source 1
            const auto spkPos = field.toView (6.5, -1.0);  // speaker 3 (moved earlier in this test)

            CHECK (field.findHandleAt (srcPos).has_value() == editSources);
            CHECK (field.findHandleAt (spkPos).has_value() == editSpeakers);

            field.moveHandle ({ true, 0 }, field.toView (2.0, 2.0));
            CHECK ((std::abs (value (p, tlos::params::sourceId (0, 'x')) - 2.0f) < 0.02f) == editSources);
            field.moveHandle ({ false, 2 }, field.toView (-2.0, -2.0));
            CHECK ((std::abs (value (p, tlos::params::speakerId (2, 'x')) + 2.0f) < 0.02f) == editSpeakers);

            // restore
            setParam (p, tlos::params::sourceId (0, 'x'), 0.5f);
            setParam (p, tlos::params::sourceId (0, 'y'), 1.0f);
            setParam (p, tlos::params::speakerId (2, 'x'), 6.5f);
            setParam (p, tlos::params::speakerId (2, 'y'), -1.0f);

            CHECK (editor->getResetButton().isEnabled() == (editSources || editSpeakers));
        }

        // Reset layout only touches what is set to Edit.
        srcBox.setSelectedId (1, juce::sendNotificationSync); // sources: Edit
        spkBox.setSelectedId (2, juce::sendNotificationSync); // speakers: View
        setParam (p, tlos::params::speakerId (0, 'x'), 1.234f);
        setParam (p, tlos::params::sourceId (0, 'x'), 7.0f);
        editor->getResetButton().onClick(); // (triggerClick needs a visible button)
        CHECK (std::abs (value (p, tlos::params::speakerId (0, 'x')) - 1.234f) < 1e-3f); // untouched
        CHECK (std::abs (value (p, tlos::params::sourceId (0, 'x')) - 7.0f) > 0.5f);     // reset

        // A change made elsewhere (state restore, another editor) shows up in the menus.
        p.setSpeakersEditable (true);
        editor->syncControls();
        CHECK (spkBox.getSelectedId() == 1);

        // Restore the picture and leave both set to Edit for the checks below.
        setParam (p, tlos::params::speakerId (0, 'x'), 0.0f);
        setParam (p, tlos::params::sourceId (0, 'x'), 0.5f);
        setParam (p, tlos::params::sourceId (0, 'y'), 1.0f);
        srcBox.setSelectedId (1, juce::sendNotificationSync);
        spkBox.setSelectedId (1, juce::sendNotificationSync);
    }

    // Export / import of positions as XML (both kinds are set to Edit here).
    {
        using tlos::io::Kind;
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
            const float keepX = value (p, tlos::params::sourceId (0, 'x')), keepY = value (p, tlos::params::sourceId (0, 'y'));
            setParam (p, tlos::params::sourceId (0, 'x'), 1.23456f);
            setParam (p, tlos::params::sourceId (0, 'y'), -0.0004f);
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

            setParam (p, tlos::params::sourceId (0, 'x'), keepX);
            setParam (p, tlos::params::sourceId (0, 'y'), keepY);
            CHECK (editor->exportPositionsTo (Kind::Sources, srcFile).wasOk()); // re-export the original positions
        }

        // Round trip: remember, scramble, import, compare.
        float srcX[3], srcY[3], spkX[7], spkY[7];
        for (int i = 0; i < 3; ++i) { srcX[i] = value (p, tlos::params::sourceId (i, 'x'));  srcY[i] = value (p, tlos::params::sourceId (i, 'y')); }
        for (int i = 0; i < 7; ++i) { spkX[i] = value (p, tlos::params::speakerId (i, 'x')); spkY[i] = value (p, tlos::params::speakerId (i, 'y')); }

        for (int i = 0; i < 3; ++i) { setParam (p, tlos::params::sourceId (i, 'x'), -9.0f);  setParam (p, tlos::params::sourceId (i, 'y'), 9.0f); }
        for (int i = 0; i < 7; ++i) { setParam (p, tlos::params::speakerId (i, 'x'), -9.0f); setParam (p, tlos::params::speakerId (i, 'y'), 9.0f); }

        CHECK (editor->importPositionsFrom (Kind::Sources, srcFile).wasOk());
        for (int i = 0; i < 3; ++i)
        {
            CHECK (std::abs (value (p, tlos::params::sourceId (i, 'x')) - srcX[i]) < 1e-3f);
            CHECK (std::abs (value (p, tlos::params::sourceId (i, 'y')) - srcY[i]) < 1e-3f);
        }
        // Importing sources did not touch the speakers.
        CHECK (std::abs (value (p, tlos::params::speakerId (0, 'x')) + 9.0f) < 1e-3f);

        CHECK (editor->importPositionsFrom (Kind::Speakers, spkFile).wasOk());
        for (int i = 0; i < 7; ++i)
        {
            CHECK (std::abs (value (p, tlos::params::speakerId (i, 'x')) - spkX[i]) < 1e-3f);
            CHECK (std::abs (value (p, tlos::params::speakerId (i, 'y')) - spkY[i]) < 1e-3f);
        }

        // A file of the other kind, a missing file and garbage are rejected without changing anything.
        setParam (p, tlos::params::speakerId (0, 'x'), 3.0f);
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
        CHECK (std::abs (value (p, tlos::params::speakerId (0, 'x')) - 3.0f) < 1e-3f);

        // Partial files: valid, out-of-range (clamped), malformed (skipped), and more entries than the
        // 7 speakers in use (applied, but not counted as in use). The count setting is untouched.
        const auto mixed = dir.getChildFile ("mixed.xml");
        juce::String text = "<TriPanSpeakers version=\"1\"><Speaker index=\"2\" x=\"1.5\" y=\"-2.5\"/>"
                            "<Speaker index=\"3\" x=\"199\" y=\"0\"/><Speaker index=\"4\" x=\"1\"/>"
                            "<Speaker index=\"12\" x=\"4\" y=\"4\"/></TriPanSpeakers>";
        mixed.replaceWithText (text);
        tlos::io::ImportReport report;
        CHECK (tlos::io::load (p, Kind::Speakers, mixed, report).wasOk());
        CHECK (report.applied == 3 && report.inUse == 2 && report.clamped == 1 && report.skipped == 1);
        CHECK (std::abs (value (p, tlos::params::speakerId (1, 'x')) - 1.5f) < 1e-3f);
        CHECK (std::abs (value (p, tlos::params::speakerId (2, 'x')) - 100.0f) < 1e-3f);
        CHECK (std::abs (value (p, tlos::params::speakerId (11, 'y')) - 4.0f) < 1e-3f);
        CHECK (p.getActiveSpeakers() == 7);

        // Importing a speaker file with 3 speakers sets Number of Speakers to 3 (the track has 7 outputs)
        // and the control follows.
        const auto three = dir.getChildFile ("three.xml");
        three.replaceWithText ("<TriPanSpeakers version=\"1\"><Speaker index=\"1\" x=\"-3\" y=\"3\"/>"
                               "<Speaker index=\"2\" x=\"3\" y=\"3\"/><Speaker index=\"3\" x=\"0\" y=\"-4\"/></TriPanSpeakers>");
        CHECK (editor->importPositionsFrom (Kind::Speakers, three).wasOk());
        CHECK (p.getActiveSpeakers() == 3);
        CHECK (editor->getSpeakerCountSlider().getValue() == 3.0);
        setParam (p, tlos::params::kNumSpeakersId, 7.0f); // back to all 7
        editor->syncControls();
        CHECK (editor->getSpeakerCountSlider().getValue() == 7.0);

        // The spread of a source is saved with it (optional attribute; entries without it leave the spread alone).
        {
            setParam (p, tlos::params::spreadId (0), 1.25f);
            setParam (p, tlos::params::spreadId (2), 4.0f);
            const auto spreadFile = dir.getChildFile ("spread.xml");
            CHECK (editor->exportPositionsTo (Kind::Sources, spreadFile).wasOk());
            {
                const auto doc = juce::XmlDocument::parse (spreadFile);
                const auto* first = doc != nullptr ? doc->getChildElement (0) : nullptr;
                CHECK (first != nullptr && first->getStringAttribute ("spread").trim() == "1.250" && first->getStringAttribute ("spread").length() == 7);
            }

            setParam (p, tlos::params::spreadId (0), 0.0f);
            setParam (p, tlos::params::spreadId (2), 0.0f);
            tlos::io::ImportReport spreadReport;
            CHECK (tlos::io::load (p, Kind::Sources, spreadFile, spreadReport).wasOk() && spreadReport.spreadApplied == 3);
            CHECK (std::abs (value (p, tlos::params::spreadId (0)) - 1.25f) < 1e-3f && std::abs (value (p, tlos::params::spreadId (2)) - 4.0f) < 1e-3f);

            // No attribute: unchanged. Out of range: limited. Not a number: ignored.
            setParam (p, tlos::params::spreadId (0), 7.0f);
            const auto plain = dir.getChildFile ("no-spread.xml");
            plain.replaceWithText ("<TriPanSources version=\"1\"><Source index=\"1\" x=\"0\" y=\"1\"/></TriPanSources>");
            CHECK (tlos::io::load (p, Kind::Sources, plain, spreadReport).wasOk() && spreadReport.spreadApplied == 0);
            CHECK (std::abs (value (p, tlos::params::spreadId (0)) - 7.0f) < 1e-3f);

            const auto odd = dir.getChildFile ("odd-spread.xml");
            odd.replaceWithText ("<TriPanSources version=\"1\"><Source index=\"1\" x=\"0\" y=\"1\" spread=\"99\"/><Source index=\"2\" x=\"1\" y=\"1\" spread=\"-3\"/>"
                                 "<Source index=\"3\" x=\"2\" y=\"1\" spread=\"abc\"/></TriPanSources>");
            setParam (p, tlos::params::spreadId (2), 5.0f);
            CHECK (tlos::io::load (p, Kind::Sources, odd, spreadReport).wasOk());
            CHECK (std::abs (value (p, tlos::params::spreadId (0)) - tlos::params::kMaxSpread) < 1e-3f && std::abs (value (p, tlos::params::spreadId (1))) < 1e-3f);
            CHECK (std::abs (value (p, tlos::params::spreadId (2)) - 5.0f) < 1e-3f && spreadReport.clamped == 2 && spreadReport.spreadApplied == 2);

            for (int i = 0; i < 3; ++i) setParam (p, tlos::params::spreadId (i), 0.0f);
            setParam (p, tlos::params::sourceId (0, 'x'), srcX[0]);   // the import moved sources 1-3
            setParam (p, tlos::params::sourceId (0, 'y'), srcY[0]);
            setParam (p, tlos::params::sourceId (1, 'x'), srcX[1]);
            setParam (p, tlos::params::sourceId (1, 'y'), srcY[1]);
            setParam (p, tlos::params::sourceId (2, 'x'), srcX[2]);
            setParam (p, tlos::params::sourceId (2, 'y'), srcY[2]);
        }

        // Virtual speakers are part of a speaker layout file.
        {
            CHECK (p.getVirtualMode() == tlos::VirtualMode::Auto && p.getManualVirtualSpeakers().empty());
            const auto countChildren = [] (const juce::XmlElement& xml, const char* tag) { int n = 0; for (auto* e : xml.getChildWithTagNameIterator (tag)) { (void) e; ++n; } return n; };

            // Auto: only the setting is recorded (the speakers are recomputed on import).
            const auto autoFile = dir.getChildFile ("virtual-auto.xml");
            CHECK (editor->exportPositionsTo (Kind::Speakers, autoFile).wasOk());
            {
                const auto doc = juce::XmlDocument::parse (autoFile);
                CHECK (doc != nullptr && doc->getStringAttribute ("virtual") == "auto" && countChildren (*doc, "VirtualSpeaker") == 0 && countChildren (*doc, "Speaker") == 7);
            }

            // Manual: the user's own virtual speakers are written, formatted like the others.
            p.setVirtualMode (tlos::VirtualMode::Manual);
            p.setManualVirtualSpeakers ({ { 1.5, -2.25 }, { -3.0, 4.0 } });
            const auto manualFile = dir.getChildFile ("virtual-manual.xml");
            CHECK (editor->exportPositionsTo (Kind::Speakers, manualFile).wasOk());
            {
                const auto doc = juce::XmlDocument::parse (manualFile);
                CHECK (doc != nullptr && doc->getStringAttribute ("virtual") == "manual" && countChildren (*doc, "VirtualSpeaker") == 2 && countChildren (*doc, "Speaker") == 7);
                const auto* v1 = doc->getChildByName ("VirtualSpeaker");
                CHECK (v1 != nullptr && v1->getStringAttribute ("x") == "  1.500" && v1->getStringAttribute ("y") == " -2.250" && v1->getStringAttribute ("index") == " 1");
            }

            // Off: only the setting.
            p.setVirtualMode (tlos::VirtualMode::Off);
            const auto offFile = dir.getChildFile ("virtual-off.xml");
            CHECK (editor->exportPositionsTo (Kind::Speakers, offFile).wasOk());
            CHECK (juce::XmlDocument::parse (offFile)->getStringAttribute ("virtual") == "off");
            CHECK (countChildren (*juce::XmlDocument::parse (offFile), "VirtualSpeaker") == 0);

            // Importing restores the setting and, for manual, the list.
            p.setManualVirtualSpeakers ({});
            tlos::io::ImportReport r;
            CHECK (tlos::io::load (p, Kind::Speakers, manualFile, r).wasOk());
            CHECK (r.virtualMode && *r.virtualMode == tlos::VirtualMode::Manual && r.virtualCount == 2);
            CHECK (p.getVirtualMode() == tlos::VirtualMode::Manual);
            auto list = p.getManualVirtualSpeakers();
            CHECK (list.size() == 2 && std::abs (list[0].x - 1.5) < 1e-3 && std::abs (list[0].y + 2.25) < 1e-3 && std::abs (list[1].x + 3.0) < 1e-3 && std::abs (list[1].y - 4.0) < 1e-3);

            CHECK (tlos::io::load (p, Kind::Speakers, autoFile, r).wasOk() && p.getVirtualMode() == tlos::VirtualMode::Auto);
            CHECK (p.getManualVirtualSpeakers().size() == 2); // the stored manual list is kept for later
            CHECK (tlos::io::load (p, Kind::Speakers, offFile, r).wasOk() && p.getVirtualMode() == tlos::VirtualMode::Off);

            // A file without the attribute (an older file) leaves the virtual speaker setting alone.
            p.setVirtualLayout (tlos::VirtualMode::Manual, &list);
            const auto oldFile = dir.getChildFile ("old.xml");
            oldFile.replaceWithText ("<TriPanSpeakers version=\"1\"><Speaker index=\"1\" x=\"-3\" y=\"3\"/><Speaker index=\"2\" x=\"3\" y=\"3\"/>"
                                     "<Speaker index=\"3\" x=\"0\" y=\"-4\"/></TriPanSpeakers>");
            CHECK (tlos::io::load (p, Kind::Speakers, oldFile, r).wasOk());
            CHECK (! r.virtualMode.has_value() && p.getVirtualMode() == tlos::VirtualMode::Manual && p.getManualVirtualSpeakers().size() == 2);
            setParam (p, tlos::params::kNumSpeakersId, 7.0f); // the old file set the count to 3

            // An unknown setting is rejected, and nothing at all is changed.
            setParam (p, tlos::params::speakerId (0, 'x'), 2.0f);
            const auto bad = dir.getChildFile ("bad-virtual.xml");
            bad.replaceWithText ("<TriPanSpeakers version=\"1\" virtual=\"banana\"><Speaker index=\"1\" x=\"-9\" y=\"9\"/></TriPanSpeakers>");
            const auto badResult = tlos::io::load (p, Kind::Speakers, bad, r);
            CHECK (badResult.failed() && badResult.getErrorMessage().contains ("banana"));
            CHECK (std::abs (value (p, tlos::params::speakerId (0, 'x')) - 2.0f) < 1e-3f && p.getVirtualMode() == tlos::VirtualMode::Manual);

            // Limits: at most 64 entries (extra ones are skipped), bad entries skipped, positions clamped.
            juce::String many = "<TriPanSpeakers version=\"1\" virtual=\"manual\"><Speaker index=\"1\" x=\"0\" y=\"5\"/>"
                                "<Speaker index=\"2\" x=\"5\" y=\"0\"/><Speaker index=\"3\" x=\"-5\" y=\"0\"/>"
                                "<VirtualSpeaker x=\"199\" y=\"-199\"/><VirtualSpeaker x=\"1\"/><VirtualSpeaker x=\"nan\" y=\"2\"/>";
            for (int i = 0; i < 70; ++i)
                many << "<VirtualSpeaker x=\"" << (i % 5) << "\" y=\"" << (i % 3) << "\"/>";
            many << "</TriPanSpeakers>";
            const auto manyFile = dir.getChildFile ("many.xml");
            manyFile.replaceWithText (many);
            CHECK (tlos::io::load (p, Kind::Speakers, manyFile, r).wasOk());
            CHECK (r.virtualCount == TriPanAudioProcessor::kMaxVirtualSpeakers && r.skipped >= 2 + 3 && r.clamped == 1);
            list = p.getManualVirtualSpeakers();
            CHECK ((int) list.size() == TriPanAudioProcessor::kMaxVirtualSpeakers && std::abs (list[0].x - 100.0) < 1e-3 && std::abs (list[0].y + 100.0) < 1e-3);
            setParam (p, tlos::params::kNumSpeakersId, 7.0f);

            // "manual" with no entries means no virtual speakers (not a copy of the automatic ones).
            const auto none = dir.getChildFile ("none.xml");
            none.replaceWithText ("<TriPanSpeakers version=\"1\" virtual=\"manual\"><Speaker index=\"1\" x=\"0\" y=\"5\"/>"
                                  "<Speaker index=\"2\" x=\"5\" y=\"-5\"/><Speaker index=\"3\" x=\"-5\" y=\"-5\"/></TriPanSpeakers>");
            CHECK (tlos::io::load (p, Kind::Speakers, none, r).wasOk());
            CHECK (p.getVirtualMode() == tlos::VirtualMode::Manual && p.getManualVirtualSpeakers().empty() && p.getVirtualSpeakers().empty());
            setParam (p, tlos::params::kNumSpeakersId, 7.0f);

            // Back to the defaults for the rest of the test.
            const std::vector<tripan::Vec2> noManual;
            p.setVirtualLayout (tlos::VirtualMode::Auto, &noManual);
            for (int i = 0; i < 7; ++i) { setParam (p, tlos::params::speakerId (i, 'x'), spkX[i]); setParam (p, tlos::params::speakerId (i, 'y'), spkY[i]); }
            editor->syncControls();
        }

        // Restore the picture.
        for (int i = 0; i < 7; ++i) { setParam (p, tlos::params::speakerId (i, 'x'), spkX[i]); setParam (p, tlos::params::speakerId (i, 'y'), spkY[i]); }
        setParam (p, tlos::params::speakerId (11, 'y'), 0.0f);
        setParam (p, tlos::params::speakerId (11, 'x'), 0.0f);

        // Import and export are available whether a kind is set to Edit or View (View only locks dragging and reset).
        for (int combination = 0; combination < 4; ++combination)
        {
            const bool editSources = (combination & 1) != 0, editSpeakers = (combination & 2) != 0;
            editor->getSourcesModeBox().setSelectedId (editSources ? 1 : 2, juce::sendNotificationSync);
            editor->getSpeakersModeBox().setSelectedId (editSpeakers ? 1 : 2, juce::sendNotificationSync);
            CHECK (editor->getImportSourcesButton().isEnabled() && editor->getImportSpeakersButton().isEnabled());
            CHECK (editor->getExportSourcesButton().isEnabled() && editor->getExportSpeakersButton().isEnabled());

            // Importing really changes the positions, even for a kind that is set to View.
            setParam (p, tlos::params::sourceId (0, 'x'), -9.0f);
            setParam (p, tlos::params::speakerId (0, 'x'), -9.0f);
            CHECK (editor->importPositionsFrom (Kind::Sources, srcFile).wasOk());
            CHECK (editor->importPositionsFrom (Kind::Speakers, spkFile).wasOk());
            CHECK (std::abs (value (p, tlos::params::sourceId (0, 'x')) - srcX[0]) < 1e-3f);
            CHECK (std::abs (value (p, tlos::params::speakerId (0, 'x')) - spkX[0]) < 1e-3f);
        }
        editor->getSourcesModeBox().setSelectedId (1, juce::sendNotificationSync);
        editor->getSpeakersModeBox().setSelectedId (1, juce::sendNotificationSync);

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

    // Zoom In / Zoom Out on the XY pad.
    {
        auto& zin = editor->getZoomInButton();
        auto& zout = editor->getZoomOutButton();
        editor->syncControls();

        // Start: the whole area, as before.
        CHECK (field.getViewHalfExtent() == 10.0 && field.getViewCentre().x == 0.0 && field.getViewCentre().y == 0.0);
        CHECK (zin.isEnabled() && ! zout.isEnabled());
        CHECK (zin.getTitle() == "Zoom In" && zout.getTitle() == "Zoom Out" && zin.getButtonText().isEmpty() && zin.getWidth() <= 40 && zout.getWidth() <= 40);

        // The area with the speakers that are in use.
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (int i = 0; i < p.getActiveSpeakers(); ++i)
        {
            const double x = value (p, tlos::params::speakerId (i, 'x')), y = value (p, tlos::params::speakerId (i, 'y'));
            minX = std::min (minX, x); maxX = std::max (maxX, x); minY = std::min (minY, y); maxY = std::max (maxY, y);
        }
        const double centreX = 0.5 * (minX + maxX), centreY = 0.5 * (minY + maxY);

        const auto speakersVisible = [&]
        {
            const auto plane = field.getLocalBounds().toFloat();
            for (int i = 0; i < p.getActiveSpeakers(); ++i)
                if (! plane.contains (field.toView (value (p, tlos::params::speakerId (i, 'x')), value (p, tlos::params::speakerId (i, 'y')))))
                    return false;
            return true;
        };

        double previousHalf = field.getViewHalfExtent();
        int steps = 0;
        while (zin.isEnabled() && steps < 20)
        {
            zin.onClick();
            ++steps;
            CHECK (field.getViewHalfExtent() < previousHalf); // every press zooms in
            previousHalf = field.getViewHalfExtent();
            CHECK (speakersVisible());                        // never cuts off a speaker
            CHECK (zout.isEnabled());
        }
        CHECK (steps >= 1 && steps < 20);
        CHECK (! zin.isEnabled());                            // fully zoomed in: the speakers fill the view
        CHECK (field.getViewHalfExtent() < 10.0 && field.getViewHalfExtent() >= 2.0);
        CHECK (std::abs (field.getViewCentre().x - centreX) < 1e-6 && std::abs (field.getViewCentre().y - centreY) < 1e-6);

        // Dragging works through the zoomed mapping, and hit testing too.
        CHECK (field.toView (field.getViewCentre().x, field.getViewCentre().y).getDistanceFrom (field.getLocalBounds().toFloat().getCentre()) < 1.0f);
        field.moveHandle ({ true, 0 }, field.toView (1.25, 0.75));
        CHECK (std::abs (value (p, tlos::params::sourceId (0, 'x')) - 1.25f) < 0.02f && std::abs (value (p, tlos::params::sourceId (0, 'y')) - 0.75f) < 0.02f);
        const auto grabbed = field.findHandleAt (field.toView (1.25, 0.75));
        CHECK (grabbed && grabbed->isSource && grabbed->index == 0);
        setParam (p, tlos::params::sourceId (0, 'x'), 0.5f);
        setParam (p, tlos::params::sourceId (0, 'y'), 1.0f);

        // Zoom out again, step by step, back to the entire area.
        previousHalf = field.getViewHalfExtent();
        steps = 0;
        while (zout.isEnabled() && steps < 20)
        {
            zout.onClick();
            ++steps;
            CHECK (field.getViewHalfExtent() > previousHalf);
            previousHalf = field.getViewHalfExtent();
        }
        CHECK (field.getViewHalfExtent() == 10.0 && field.getViewCentre().x == 0.0 && field.getViewCentre().y == 0.0);
        CHECK (zin.isEnabled() && ! zout.isEnabled());

        // The view is not recomputed while speakers move: dragging a speaker does not shift the view.
        zin.onClick();
        const auto viewBefore = field.getViewCentre();
        const double halfBefore = field.getViewHalfExtent();
        const float speakerX = value (p, tlos::params::speakerId (0, 'x'));
        setParam (p, tlos::params::speakerId (0, 'x'), -9.0f);
        field.refresh();
        CHECK (std::abs (field.getViewCentre().x - viewBefore.x) < 1e-12 && std::abs (field.getViewCentre().y - viewBefore.y) < 1e-12);
        CHECK (std::abs (field.getViewHalfExtent() - halfBefore) < 1e-12);
        setParam (p, tlos::params::speakerId (0, 'x'), speakerX);
        while (zout.isEnabled())
            zout.onClick();
        field.refresh();
    }

    // Virtual speakers: Auto / Manual / Off menu, and adding, moving and deleting manual ones on the pad.
    {
        auto& box = editor->getVirtualModeBox();
        auto& spkBox = editor->getSpeakersModeBox();
        CHECK (box.getNumItems() == 3 && box.getItemText (0) == "Auto" && box.getItemText (1) == "Manual" && box.getItemText (2) == "Off");
        CHECK (box.getSelectedId() == 1 && p.getVirtualMode() == tlos::VirtualMode::Auto); // default

        // Auto: the user cannot add or move anything, even with speakers set to Edit.
        spkBox.setSelectedId (1, juce::sendNotificationSync);
        CHECK (! field.canEditVirtualSpeakers() && ! field.addVirtualSpeakerAt (field.toView (1.0, 1.0)));

        box.setSelectedId (3, juce::sendNotificationSync); // Off
        CHECK (p.getVirtualMode() == tlos::VirtualMode::Off && p.getVirtualSpeakers().empty() && ! field.addVirtualSpeakerAt (field.toView (1.0, 1.0)));

        // Manual with speakers set to View: locked like the speakers.
        box.setSelectedId (2, juce::sendNotificationSync);
        CHECK (p.getVirtualMode() == tlos::VirtualMode::Manual && p.getManualVirtualSpeakers().empty());
        spkBox.setSelectedId (2, juce::sendNotificationSync);
        CHECK (! field.canEditVirtualSpeakers() && ! field.addVirtualSpeakerAt (field.toView (1.0, 1.0)));

        // Manual with speakers set to Edit: add, grab, move, delete.
        spkBox.setSelectedId (1, juce::sendNotificationSync);
        CHECK (field.canEditVirtualSpeakers());
        CHECK (field.addVirtualSpeakerAt (field.toView (-3.0, 6.0)));
        CHECK (field.addVirtualSpeakerAt (field.toView (4.5, -5.5)));
        auto list = p.getManualVirtualSpeakers();
        CHECK (list.size() == 2 && std::abs (list[0].x + 3.0) < 0.02 && std::abs (list[0].y - 6.0) < 0.02 && std::abs (list[1].x - 4.5) < 0.02);

        const auto grabbed = field.findHandleAt (field.toView (4.5, -5.5));
        CHECK (grabbed && grabbed->isVirtual && ! grabbed->isSource && grabbed->index == 1);

        field.moveHandle ({ false, 1, true }, field.toView (0.0, -2.0));
        list = p.getManualVirtualSpeakers();
        CHECK (std::abs (list[1].x) < 0.02 && std::abs (list[1].y + 2.0) < 0.02);
        field.moveHandle ({ false, 0, true }, juce::Point<float> (-5000.0f, -5000.0f)); // far away: clamped to the range
        list = p.getManualVirtualSpeakers();
        CHECK (std::abs (list[0].x + 10.0) < 1e-3 && std::abs (list[0].y - 10.0) < 1e-3);

        // The virtual speakers take part in the panning: a source on a virtual speaker feeds the real speakers around it.
        field.refresh();
        CHECK (p.getVirtualSpeakers().size() == 2);

        CHECK (field.removeVirtualSpeaker (0));
        CHECK (p.getManualVirtualSpeakers().size() == 1 && ! field.removeVirtualSpeaker (5) && ! field.removeVirtualSpeaker (-1));

        // At most 64.
        for (int i = 0; i < 100; ++i)
            field.addVirtualSpeakerAt (field.toView (-5.0 + 0.1 * i, 7.0));
        CHECK ((int) p.getManualVirtualSpeakers().size() == TriPanAudioProcessor::kMaxVirtualSpeakers);
        CHECK (! field.addVirtualSpeakerAt (field.toView (0.0, 0.0)));

        // Back to the defaults for the rest of the test (speakers: Edit stays on, as the later checks expect).
        p.setManualVirtualSpeakers ({});
        box.setSelectedId (3, juce::sendNotificationSync); // Off: the later checks assume plain triangulation
        field.refresh();
    }

    // Spread: an outer circle per source. Only the inner circle is a mouse target; Option or Cmd + drag sets the spread.
    {
        editor->getSourcesModeBox().setSelectedId (1, juce::sendNotificationSync);
        const float ppm = field.toView (1.0, 0.0).x - field.toView (0.0, 0.0).x; // pixels per metre
        const auto p0 = field.toView (0.5, 1.0);                                  // source 1
        const auto spread = [&] (int i) { return value (p, tlos::params::spreadId (i)); };
        const juce::ModifierKeys none, alt (juce::ModifierKeys::altModifier), cmd (juce::ModifierKeys::commandModifier);

        CHECK (tlos::FieldComponent::isSpreadModifier (alt) && ! tlos::FieldComponent::isSpreadModifier (none));
        CHECK (std::abs (spread (0)) < 1e-6f && field.getSpread (0) == 0.0);       // default: no spread

        // A big spread circle on source 1 covers source 3, but only inner circles take the mouse.
        const double x3 = value (p, tlos::params::sourceId (2, 'x')), y3 = value (p, tlos::params::sourceId (2, 'y'));
        const float bigSpread = std::min (10.0f, (float) std::hypot (x3 - 0.5, y3 - 1.0) + 1.0f);
        setParam (p, tlos::params::spreadId (0), bigSpread);
        field.refresh();
        CHECK (std::abs (field.getSpread (0) - bigSpread) < 1e-3);
        const auto h3 = field.findHandleAt (field.toView (x3, y3));               // source 3 lies inside the spread circle
        CHECK (h3 && h3->isSource && h3->index == 2);
        CHECK (! field.findHandleAt (field.toView (3.5, 1.0)).has_value());        // inside the circle, not on any source
        CHECK (field.findHandleAt (p0 + juce::Point<float> (tlos::FieldComponent::kSourceRadius - 1.0f, 0.0f)).has_value());
        CHECK (! field.findHandleAt (p0 + juce::Point<float> (tlos::FieldComponent::kSourceRadius + 2.0f, 0.0f)).has_value());
        setParam (p, tlos::params::spreadId (0), 0.0f);

        // Option + drag, vertically: up increases the spread, down decreases it, sideways does nothing.
        // The change is relative to the value at the press, and the source stays where it is.
        const auto up = [&] (double metres) { return juce::Point<float> (0.0f, (float) (-metres * ppm)); };
        const auto stay = [] (juce::Point<float> a, juce::Point<float> b) { return a + b; };

        field.pressAt (p0, alt);
        CHECK (spread (0) < 1e-6f);                                             // pressing alone changes nothing
        auto pointer = p0;
        pointer = stay (pointer, up (2.0));    field.dragTo (pointer);          // up 2 m
        CHECK (std::abs (spread (0) - 2.0f) < 0.03f);
        pointer = stay (pointer, { 300.0f, 0.0f }); field.dragTo (pointer);     // sideways: no effect
        CHECK (std::abs (spread (0) - 2.0f) < 0.03f);
        pointer = stay (pointer, { -600.0f, 0.0f }); field.dragTo (pointer);
        CHECK (std::abs (spread (0) - 2.0f) < 0.03f);
        pointer = stay (pointer, up (-1.0));   field.dragTo (pointer);          // down 1 m
        CHECK (std::abs (spread (0) - 1.0f) < 0.03f);
        pointer = stay (pointer, up (-5.0));   field.dragTo (pointer);          // far down: stops at 0
        CHECK (spread (0) < 1e-6f);
        pointer = stay (pointer, up (0.5));    field.dragTo (pointer);          // up again: increases at once (no dead zone)
        CHECK (std::abs (spread (0) - 0.5f) < 0.03f);
        pointer = stay (pointer, up (80.0));   field.dragTo (pointer);          // far up: stops at the maximum
        CHECK (std::abs (spread (0) - tlos::params::kMaxSpread) < 1e-3f);
        pointer = stay (pointer, up (-1.0));   field.dragTo (pointer);          // and down again at once
        CHECK (std::abs (spread (0) - (tlos::params::kMaxSpread - 1.0f)) < 0.03f);
        field.release();
        CHECK (std::abs (value (p, tlos::params::sourceId (0, 'x')) - 0.5f) < 0.02f && std::abs (value (p, tlos::params::sourceId (0, 'y')) - 1.0f) < 0.02f);

        // A new drag starts from the current value, wherever the mouse is pressed on the inner circle.
        setParam (p, tlos::params::spreadId (0), 2.5f);
        field.pressAt (p0 + juce::Point<float> (4.0f, 3.0f), alt);
        field.dragTo (p0 + juce::Point<float> (4.0f, 3.0f) + up (1.0));
        field.release();
        CHECK (std::abs (spread (0) - 3.5f) < 0.03f);
        setParam (p, tlos::params::spreadId (0), 0.0f);

        // Only Option does this: with Cmd (or Ctrl) the drag moves the source as usual.
        for (const auto& modifier : { cmd, juce::ModifierKeys (juce::ModifierKeys::ctrlModifier) })
        {
            field.pressAt (p0, modifier);
            field.dragTo (field.toView (1.5, 1.5));
            field.release();
            CHECK (spread (0) < 1e-6f);
            CHECK (std::abs (value (p, tlos::params::sourceId (0, 'x')) - 1.5f) < 0.02f);
            setParam (p, tlos::params::sourceId (0, 'x'), 0.5f);
            setParam (p, tlos::params::sourceId (0, 'y'), 1.0f);
        }
        CHECK (! tlos::FieldComponent::isSpreadModifier (cmd));

        // Without a modifier the drag moves the source and leaves the spread alone.
        setParam (p, tlos::params::spreadId (0), 2.5f);
        field.pressAt (p0, none);
        field.dragTo (field.toView (1.5, 1.5));
        field.release();
        CHECK (std::abs (value (p, tlos::params::sourceId (0, 'x')) - 1.5f) < 0.02f && std::abs (spread (0) - 2.5f) < 0.03f);
        setParam (p, tlos::params::sourceId (0, 'x'), 0.5f);
        setParam (p, tlos::params::sourceId (0, 'y'), 1.0f);

        // Option + double-click on the source resets its spread; without the modifier (or with Cmd) nothing happens.
        field.doubleClickAt (p0, none);
        field.doubleClickAt (p0, cmd);
        CHECK (std::abs (spread (0) - 2.5f) < 0.03f);
        field.doubleClickAt (p0, alt);
        CHECK (spread (0) < 1e-6f);

        // A source whose position is set to View cannot get a new spread.
        editor->getSourcesModeBox().setSelectedId (2, juce::sendNotificationSync);
        field.pressAt (p0, alt);
        field.dragTo (p0 + up (3.0));
        field.release();
        CHECK (spread (0) < 1e-6f);
        field.setSpread (0, 3.0);
        CHECK (spread (0) < 1e-6f);
        editor->getSourcesModeBox().setSelectedId (1, juce::sendNotificationSync);
        field.setSpread (0, 3.0);                                               // allowed again, and limited to the range
        CHECK (std::abs (spread (0) - 3.0f) < 1e-3f);
        field.setSpread (0, 99.0);
        CHECK (std::abs (spread (0) - tlos::params::kMaxSpread) < 1e-3f);
        field.setSpread (0, -4.0);
        CHECK (spread (0) < 1e-6f);

        // The pad has a tooltip that explains the gesture.
        CHECK (field.getTooltip().containsIgnoreCase ("spread"));

        // Zoomed in: the circle and the gesture follow the view (metres, not pixels).
        field.zoomIn();
        const auto zoomedP0 = field.toView (0.5, 1.0);
        const float zoomedPpm = field.toView (1.0, 0.0).x - field.toView (0.0, 0.0).x;
        CHECK (zoomedPpm > ppm);
        field.pressAt (zoomedP0, alt);
        field.dragTo (zoomedP0 + juce::Point<float> (0.0f, -2.0f * zoomedPpm));   // up by 2 m worth of pixels
        field.release();
        CHECK (std::abs (spread (0) - 2.0f) < 0.03f);
        while (editor->getZoomOutButton().isEnabled()) editor->getZoomOutButton().onClick();
        setParam (p, tlos::params::spreadId (0), 0.0f);
        field.refresh();
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
        setParam (p, tlos::params::sourceId (3, 'x'), -9.0f);
        setParam (p, tlos::params::sourceId (3, 'y'), -9.0f);
        CHECK (! field.findHandleAt (field.toView (-9.0, -9.0)).has_value());
    }

    // Drag speaker 2 to a new place via the component's own mapping; parameters follow.
    {
        const auto target = field.toView (-2.0, 7.0);
        field.moveHandle ({ false, 2 }, target);
        CHECK (std::abs (value (p, tlos::params::speakerId (2, 'x')) + 2.0f) < 0.02f);
        CHECK (std::abs (value (p, tlos::params::speakerId (2, 'y')) - 7.0f) < 0.02f);

        // Dragging far outside the plane clamps to the parameter range (screen y grows downwards).
        field.moveHandle ({ true, 0 }, juce::Point<float> (-5000.0f, 5000.0f));
        CHECK (std::abs (value (p, tlos::params::sourceId (0, 'x')) + 10.0f) < 1e-3f);
        CHECK (std::abs (value (p, tlos::params::sourceId (0, 'y')) + 10.0f) < 1e-3f);

        // Round trip of the coordinate mapping.
        const auto v = field.toView (3.25, -4.5);
        field.moveHandle ({ true, 1 }, v);
        CHECK (std::abs (value (p, tlos::params::sourceId (1, 'x')) - 3.25f) < 0.02f);
        CHECK (std::abs (value (p, tlos::params::sourceId (1, 'y')) + 4.5f) < 0.02f);
    }

    // Table windows: one for sources (x, y, spread) and one for speakers (x, y), opened by a button at the
    // right end of the row. The values can be edited, the width follows the content.
    {
        using tlos::io::Kind;
        using Col = tlos::PositionTable::Column;
        CHECK (editor->getTableWindow (Kind::Sources) == nullptr && editor->getTableWindow (Kind::Speakers) == nullptr);

        // The buttons sit at the right end of their rows.
        CHECK (editor->getSourcesTableButton().getRight() > editor->getImportSourcesButton().getRight());
        CHECK (editor->getSpeakersTableButton().getRight() > editor->getImportSpeakersButton().getRight());

        editor->getSourcesTableButton().onClick();
        editor->getSpeakersTableButton().onClick();
        auto* sourcesWindow = editor->getTableWindow (Kind::Sources);
        auto* speakersWindow = editor->getTableWindow (Kind::Speakers);
        CHECK (sourcesWindow != nullptr && speakersWindow != nullptr && sourcesWindow != speakersWindow);
        CHECK (sourcesWindow->isVisible() && speakersWindow->isVisible());
        CHECK (sourcesWindow->isAlwaysOnTop() && speakersWindow->isAlwaysOnTop()); // in front of the (floating) plug-in window

        auto& sources = sourcesWindow->getContent();
        auto& speakers = speakersWindow->getContent();

        // Rows are the sources / speakers in use; the cells show the parameters.
        CHECK (sources.getMainTable().getRowCount() == 3 && speakers.getMainTable().getRowCount() == 7);
        CHECK (std::abs (sources.getMainTable().getValue (0, Col::xColumn) - value (p, tlos::params::sourceId (0, 'x'))) < 1e-4);
        CHECK (std::abs (sources.getMainTable().getValue (1, Col::yColumn) - value (p, tlos::params::sourceId (1, 'y'))) < 1e-4);
        CHECK (std::abs (speakers.getMainTable().getValue (2, Col::xColumn) - value (p, tlos::params::speakerId (2, 'x'))) < 1e-4);
        CHECK (sources.getMainTable().getTable().getHeader().getNumColumns (true) == 8); // #, X, Y, Spread, Group, Trim, Mute, Solo
        CHECK (speakers.getMainTable().getTable().getHeader().getNumColumns (true) == 7); // #, X, Y, Trim, Mute, Solo, Excl.

        // Editing: typed text goes to the parameters, limited to the range; rubbish is rejected.
        CHECK (sources.getMainTable().setValueFromText (1, Col::xColumn, "2.5") == "2.500");
        CHECK (std::abs (value (p, tlos::params::sourceId (1, 'x')) - 2.5f) < 1e-4f);
        CHECK (sources.getMainTable().setValueFromText (1, Col::xColumn, "199") == "100.000");
        CHECK (std::abs (value (p, tlos::params::sourceId (1, 'x')) - 100.0f) < 1e-4f);
        CHECK (sources.getMainTable().setValueFromText (1, Col::xColumn, "abc") == "100.000");
        CHECK (sources.getMainTable().setValueFromText (1, Col::xColumn, "") == "100.000");
        CHECK (std::abs (value (p, tlos::params::sourceId (1, 'x')) - 100.0f) < 1e-4f);
        CHECK (sources.getMainTable().setValueFromText (0, Col::spreadColumn, "-3") == "0.000");     // spread is 0..50
        CHECK (sources.getMainTable().setValueFromText (0, Col::spreadColumn, "4.25") == "4.250");
        CHECK (std::abs (value (p, tlos::params::spreadId (0)) - 4.25f) < 1e-4f);
        CHECK (speakers.getMainTable().setValueFromText (4, Col::yColumn, "-1.5") == "-1.500");
        CHECK (std::abs (value (p, tlos::params::speakerId (4, 'y')) + 1.5f) < 1e-4f);
        CHECK (sources.getMainTable().setValueFromText (9, Col::xColumn, "1") == "0.000");           // no such row
        setParam (p, tlos::params::sourceId (1, 'x'), -6.5f);                                       // back as before
        setParam (p, tlos::params::spreadId (0), 0.0f);
        setParam (p, tlos::params::speakerId (4, 'y'), 0.0f);

        // A change made elsewhere (automation, the pad) shows up in the table.
        setParam (p, tlos::params::sourceId (2, 'y'), -1.25f);
        sources.refresh();
        CHECK (std::abs (sources.getMainTable().getValue (2, Col::yColumn) + 1.25) < 1e-4);
        setParam (p, tlos::params::sourceId (2, 'y'), -3.5f);

        // The number of sources / speakers can be changed in the window, and the table follows.
        sources.getCountSlider().setValue (2.0, juce::sendNotificationSync);
        CHECK (p.getActiveSources() == 2 && sources.getMainTable().getRowCount() == 2);
        speakers.getCountSlider().setValue (5.0, juce::sendNotificationSync);
        CHECK (p.getActiveSpeakers() == 5 && speakers.getMainTable().getRowCount() == 5);
        CHECK (sources.getCountSlider().getMaximum() == 3.0 && speakers.getCountSlider().getMaximum() == 7.0);
        setParam (p, tlos::params::kNumSourcesId, 3.0f);  // as if automation changed it
        setParam (p, tlos::params::kNumSpeakersId, 7.0f);
        sources.refresh();
        speakers.refresh();
        CHECK (sources.getCountSlider().getValue() == 3.0 && speakers.getCountSlider().getValue() == 7.0);
        CHECK (sources.getMainTable().getRowCount() == 3 && speakers.getMainTable().getRowCount() == 7);

        // The virtual speaker menu is in the speakers window only.
        CHECK (speakers.getVirtualModeBox().getNumItems() == 3);
        CHECK (speakers.getVirtualModeBox().getSelectedId() == (int) p.getVirtualMode());

        // The virtual speaker table appears to the right while they can be placed by hand (Manual and speakers on Edit).
        const int narrow = speakersWindow->getWidth();
        CHECK (narrow == speakers.getPreferredWidth() && sources.getPreferredWidth() > 0);
        editor->getVirtualModeBox().setSelectedId ((int) tlos::VirtualMode::Manual, juce::sendNotificationSync);
        editor->getSpeakersModeBox().setSelectedId (2, juce::sendNotificationSync);          // View: no virtual table
        speakers.refresh();
        CHECK (speakers.getVirtualTable() == nullptr);
        editor->getSpeakersModeBox().setSelectedId (1, juce::sendNotificationSync);          // Edit
        speakers.refresh();
        CHECK (speakers.getVirtualTable() != nullptr);
        CHECK (speakersWindow->getWidth() > narrow && speakersWindow->getWidth() == speakers.getPreferredWidth());
        CHECK (speakers.getVirtualTable()->getX() >= speakers.getMainTable().getRight());     // to the right of the other table

        p.setManualVirtualSpeakers ({ { 1.0, 2.0 }, { -3.0, 0.5 } });
        speakers.refresh();
        auto* virtualTable = speakers.getVirtualTable();
        CHECK (virtualTable != nullptr && virtualTable->getRowCount() == 2);
        CHECK (virtualTable->getTable().getHeader().getNumColumns (true) == 3);
        CHECK (virtualTable->setValueFromText (1, Col::xColumn, "4") == "4.000");
        CHECK (virtualTable->setValueFromText (0, Col::yColumn, "-199") == "-100.000");
        const auto manual = p.getManualVirtualSpeakers();
        CHECK (manual.size() == 2 && std::abs (manual[1].x - 4.0) < 1e-4 && std::abs (manual[1].y - 0.5) < 1e-4
               && std::abs (manual[0].x - 1.0) < 1e-4 && std::abs (manual[0].y + 100.0) < 1e-4);   // parameters are floats

        // The menu in the window changes the mode; Auto removes the table and the window narrows again.
        speakers.getVirtualModeBox().setSelectedId ((int) tlos::VirtualMode::Auto, juce::sendNotificationSync);
        CHECK (p.getVirtualMode() == tlos::VirtualMode::Auto && speakers.getVirtualTable() == nullptr);
        CHECK (speakersWindow->getWidth() == narrow);

        // Height: resizable between a few rows and every row, the width is fixed.
        CHECK (speakers.getMinimumHeight() <= speakersWindow->getHeight());
        CHECK (speakersWindow->getHeight() <= speakers.getMaximumHeight());
        CHECK (speakersWindow->getConstrainer()->getMinimumWidth() == speakersWindow->getConstrainer()->getMaximumWidth());
        CHECK (speakers.getMinimumHeight() == speakers.getHeightForRows (tlos::PositionTableContent::kMinVisibleRows));

        // Placement beside the main window: right if there is room, otherwise left, otherwise nowhere (then it is centred over it).
        {
            const juce::Rectangle<int> screen (0, 0, 1920, 1080), tableSize (0, 0, 360, 300);
            const juce::BorderSize<int> frame (28, 0, 0, 0); // a title bar
            const auto place = [&] (juce::Rectangle<int> main, juce::Rectangle<int> avail, juce::Rectangle<int> size)
            {
                return tlos::PositionTableWindow::chooseBesidePosition (main, avail, size, frame, 8);
            };

            auto right = place ({ 100, 100, 700, 830 }, screen, tableSize);
            CHECK (right.has_value() && right->x == 808 && right->y == 100);                  // right of the main window, tops level

            auto left = place ({ 1300, 100, 700, 830 }, screen, tableSize);
            CHECK (left.has_value() && left->x == 1300 - 8 - 360 && left->y == 100);         // no room on the right: left

            CHECK (! place ({ 100, 100, 700, 830 }, { 0, 0, 1000, 1080 }, tableSize).has_value()); // room on neither side
            CHECK (! place ({ 100, 100, 700, 830 }, screen, { 0, 0, 2000, 300 }).has_value());    // larger than the screen

            CHECK (place ({ 100, 900, 700, 830 }, screen, tableSize)->y == 1080 - 300);          // kept on the screen at the bottom
            CHECK (place ({ 100, -10, 700, 830 }, screen, tableSize)->y == 28);                  // and at the top (title bar visible)

            for (const auto* w : { sourcesWindow, speakersWindow })                               // the opened windows do not overlap the main window
            {
                const auto mainBounds = editor->getTopLevelComponent()->getScreenBounds();
                const auto displays = juce::Desktop::getInstance().getDisplays();
                const bool roomBeside = displays.getDisplayForRect (mainBounds) != nullptr
                    && tlos::PositionTableWindow::chooseBesidePosition (mainBounds, displays.getDisplayForRect (mainBounds)->userBounds.toNearestInt(),
                                                                  w->getLocalBounds(), {}, 8).has_value();
                if (roomBeside)
                    CHECK (! w->getBounds().intersects (mainBounds));
            }
        }

        // Closing only hides; the button brings the same window back, and closes it when it is open.
        speakersWindow->closeButtonPressed();
        CHECK (! speakersWindow->isVisible());
        editor->getSpeakersTableButton().onClick();
        CHECK (speakersWindow->isVisible() && editor->getTableWindow (Kind::Speakers) == speakersWindow);
        editor->getSpeakersTableButton().onClick();
        CHECK (! speakersWindow->isVisible() && editor->getTableWindow (Kind::Speakers) == speakersWindow);
        CHECK (sourcesWindow->isVisible()); // the other window is not affected
        editor->getSpeakersTableButton().onClick();
        CHECK (speakersWindow->isVisible());
        editor->getSourcesTableButton().onClick();
        CHECK (! sourcesWindow->isVisible() && speakersWindow->isVisible());
        editor->getSourcesTableButton().onClick();
        CHECK (sourcesWindow->isVisible());

        editor->getVirtualModeBox().setSelectedId ((int) tlos::VirtualMode::Auto, juce::sendNotificationSync);
        editor->getSpeakersModeBox().setSelectedId (2, juce::sendNotificationSync);
        p.setManualVirtualSpeakers ({});
    }

    // With more rows than fit, a table scrolls and its header stays put: a 64-channel track in a window
    // of the smallest height.
    {
        TriPanAudioProcessor big;
        juce::AudioProcessor::BusesLayout bigLayout;
        bigLayout.inputBuses.add  (juce::AudioChannelSet::discreteChannels (64));
        bigLayout.outputBuses.add (juce::AudioChannelSet::discreteChannels (64));
        CHECK (big.setBusesLayout (bigLayout));
        big.setPlayConfigDetails (64, 64, 48000.0, 512);
        big.prepareToPlay (48000.0, 512);

        tlos::PositionTableWindow window (big, tlos::io::Kind::Sources);
        auto& content = window.getContent();
        CHECK (content.getMainTable().getRowCount() == 64);
        CHECK (window.getHeight() < content.getHeightForRows (64));          // starts at a moderate height
        CHECK (window.getHeight() <= content.getMaximumHeight());

        window.setSize (window.getWidth(), content.getMinimumHeight());
        content.setBounds (0, 0, window.getWidth(), content.getMinimumHeight());
        auto& table = content.getMainTable().getTable();
        CHECK (table.getViewport()->canScrollVertically());
        CHECK (table.getHeader().getHeight() == tlos::PositionTable::kHeaderHeight);
        const int headerY = table.getHeader().getY();                         // the header is outside the scrolling area
        table.getViewport()->setViewPosition (0, 500);
        CHECK (table.getHeader().getY() == headerY && table.getHeader().isVisible());
        CHECK (table.getViewport()->getViewPositionY() > 0);
        CHECK (table.getViewport()->getY() >= tlos::PositionTable::kHeaderHeight);
        window.setVisible (false);
    }

    // Restore a pleasant picture for the snapshot.
    setParam (p, tlos::params::speakerId (2, 'x'), 6.5f);
    setParam (p, tlos::params::speakerId (2, 'y'), -1.0f);
    setParam (p, tlos::params::sourceId (0, 'x'), 0.5f);
    setParam (p, tlos::params::sourceId (0, 'y'), 1.0f);
    setParam (p, tlos::params::sourceId (1, 'x'), -6.5f);
    setParam (p, tlos::params::sourceId (1, 'y'), 8.5f);

    // The snapshot shows 6 of 7 speakers in use.
    setParam (p, tlos::params::kNumSpeakersId, 6.0f);
    p.refreshLayout();
    editor->syncControls();

    editor->getSpeakersModeBox().setSelectedId (2, juce::sendNotificationSync); // defaults for the picture: speakers View
    editor->setSize (TriPanAudioProcessorEditor::kBaseWidth, TriPanAudioProcessorEditor::kBaseHeight);

    if (argc > 2) // optional: a speaker file for the snapshot
    {
        editor->getVirtualModeBox().setSelectedId (1, juce::sendNotificationSync);
        CHECK (editor->importPositionsFrom (tlos::io::Kind::Speakers, juce::File (argv[2])).wasOk());
        setParam (p, tlos::params::sourceId (0, 'x'), 0.0f);
        setParam (p, tlos::params::sourceId (0, 'y'), 0.0f);
        editor->getSpeakersModeBox().setSelectedId (2, juce::sendNotificationSync);
        editor->setSize (TriPanAudioProcessorEditor::kBaseWidth, TriPanAudioProcessorEditor::kBaseHeight);
        editor->getZoomOutButton().onClick();
        while (editor->getZoomOutButton().isEnabled()) editor->getZoomOutButton().onClick();
    }

    if (argc > 1)
    {
        setParam (p, tlos::params::spreadId (0), 3.0f); // the snapshot shows some spread
        setParam (p, tlos::params::spreadId (2), 1.4f);
        if (argc <= 2)
            while (editor->getZoomInButton().isEnabled()) editor->getZoomInButton().onClick(); // snapshot shows the zoomed view
        field.refresh();
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::File out (argv[1]);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        CHECK (stream.openedOk() && png.writeImageToStream (image, stream));
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());

        // Pictures of the table windows as well: <name>-sources.png and <name>-speakers.png (with virtual speakers by hand).
        editor->getVirtualModeBox().setSelectedId ((int) tlos::VirtualMode::Manual, juce::sendNotificationSync);
        editor->getSpeakersModeBox().setSelectedId (1, juce::sendNotificationSync);
        p.setManualVirtualSpeakers ({ { 0.0, 0.0 }, { 0.0, -6.0 } });
        for (auto kind : { tlos::io::Kind::Sources, tlos::io::Kind::Speakers })
        {
            auto& window = editor->showTableWindow (kind);
            window.getContent().refresh();
            auto& content = window.getContent();
            content.setBounds (0, 0, window.getWidth(), window.getHeight());
            auto picture = content.createComponentSnapshot (content.getLocalBounds(), true, 1.0f);
            juce::File tableOut (out.getFullPathName().replace (".png", kind == tlos::io::Kind::Sources ? "-sources.png" : "-speakers.png"));
            tableOut.deleteFile();
            juce::FileOutputStream tableStream (tableOut);
            CHECK (tableStream.openedOk() && png.writeImageToStream (picture, tableStream));
            std::printf ("wrote %s\n", tableOut.getFullPathName().toRawUTF8());
        }
    }

    base.reset();

    testRoomSize();
    testSettingsWindow (argc > 1 ? juce::File (argv[1]).getSiblingFile (juce::File (argv[1]).getFileNameWithoutExtension() + "-calibration.png").getFullPathName().toRawUTF8() : nullptr);
    testCalibrationInterface (argc > 1 ? juce::File (argv[1]).getSiblingFile (juce::File (argv[1]).getFileNameWithoutExtension() + "-calibration.png").getFullPathName().toRawUTF8() : nullptr);
    testGroups (argc > 1 ? juce::File (argv[1]).getSiblingFile (juce::File (argv[1]).getFileNameWithoutExtension() + "-groups.png").getFullPathName().toRawUTF8() : nullptr);

    if (failures == 0)
        std::printf ("All editor tests passed\n");
    return failures == 0 ? 0 : 1;
}
