#pragma once

#include "FieldComponent.h"
#include "PluginProcessor.h"
#include "PositionIO.h"

class TriPanAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit TriPanAudioProcessorEditor (TriPanAudioProcessor&);
    ~TriPanAudioProcessorEditor() override;

    /** The GUI is laid out at this size and scaled uniformly (with a fixed aspect ratio) when the
        window is resized. The base size is also the smallest allowed size. */
    static constexpr int kBaseWidth = 700;
    static constexpr int kBaseHeight = 830;
    static constexpr int kMaxScale = 3;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Current display scale (1.0 at the base size). */
    float getScale() const noexcept { return scale; }

    FieldComponent& getField() noexcept { return field; }
    juce::Slider& getSourceCountSlider() noexcept { return sourceCount; }
    juce::Slider& getSpeakerCountSlider() noexcept { return speakerCount; }
    juce::ComboBox& getSourcesModeBox() noexcept { return sourcesModeBox; }
    juce::ComboBox& getSpeakersModeBox() noexcept { return speakersModeBox; }
    juce::ComboBox& getVirtualModeBox() noexcept { return virtualModeBox; }
    juce::TextButton& getResetButton() noexcept { return resetButton; }
    juce::TextButton& getZoomInButton() noexcept { return zoomInButton; }
    juce::TextButton& getZoomOutButton() noexcept { return zoomOutButton; }
    juce::TextButton& getExportSourcesButton() noexcept { return exportSourcesButton; }
    juce::TextButton& getImportSourcesButton() noexcept { return importSourcesButton; }
    juce::TextButton& getExportSpeakersButton() noexcept { return exportSpeakersButton; }
    juce::TextButton& getImportSpeakersButton() noexcept { return importSpeakersButton; }

    /** Writes / reads a positions file and shows the outcome in the status text. The buttons call
        these after the user has chosen a file; failures are returned, not shown in a dialog. */
    juce::Result exportPositionsTo (tripan::io::Kind, const juce::File&);
    juce::Result importPositionsFrom (tripan::io::Kind, const juce::File&);

    /** Re-reads the parameters and channel counts into the controls (also called by a timer). */
    void syncControls();

private:
    void timerCallback() override { syncControls(); }
    void layoutContent();

    void chooseFileAndExport (tripan::io::Kind);
    void chooseFileAndImport (tripan::io::Kind);
    void showStatus (const juce::String&);

    /** Wires a count slider to its parameter. The slider shows the effective value (parameter
        clamped to the track's channel count) and its maximum is the channel count. */
    void setUpCountSlider (juce::Slider&, const juce::String& parameterId);
    void syncCountSlider (juce::Slider&, int effectiveValue, int channels);

    TriPanAudioProcessor& owner;

    // All controls live in `content`, which is laid out at the base size and scaled by a transform.
    // (Declared before the controls so that it is destroyed after them.)
    juce::Component content;
    float scale = 1.0f;
    bool sizeIsRestored = false;

    FieldComponent field;

    juce::Label sourcesLabel  { {}, "Sources" };
    juce::Label speakersLabel { {}, "Speakers" };
    juce::Slider sourceCount, speakerCount;
    juce::Label infoLabel;
    juce::ComboBox sourcesModeBox, speakersModeBox; // "Edit" / "View"
    juce::Label virtualLabel { {}, "Virtual speakers" };
    juce::ComboBox virtualModeBox;                  // Auto / Manual / Off
    juce::ToggleButton showTriangulationButton { "Show triangulation" };
    juce::TextButton resetButton { "Reset layout" };
    juce::TextButton zoomInButton { "Zoom In" };
    juce::TextButton zoomOutButton { "Zoom Out" };
    juce::TextButton exportSourcesButton  { "Export sources..." };
    juce::TextButton importSourcesButton  { "Import sources..." };
    juce::TextButton exportSpeakersButton { "Export speakers..." };
    juce::TextButton importSpeakersButton { "Import speakers..." };

    std::unique_ptr<juce::FileChooser> fileChooser; // kept alive while a dialog is open
    juce::String statusText;
    juce::uint32 statusUntil = 0;

    bool syncing = false;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriPanAudioProcessorEditor)
};
