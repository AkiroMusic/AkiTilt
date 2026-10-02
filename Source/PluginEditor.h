#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PresetManager.h"
#include "PluginProcessor.h"
#include "UI/AkiLookAndFeel.h"
#include "UI/LevelMeter.h"
#include "UI/MintKnob.h"
#include "UI/PresetBar.h"
#include "UI/XYPad.h"

namespace aki::ui
{

// Small padlock button overlaid on a locked knob; clicking it releases
// that axis lock (kept in sync with the X/Y pills on the pad).
class LockBadgeButton final : public juce::Button
{
public:
    LockBadgeButton() : juce::Button ("Lock") {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        if (! isEnabled())
            g.setOpacity (0.4f);

        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        juce::Path p;
        p.addRoundedRectangle (bounds, 4.0f);
        g.setColour (down ? theme::colours::surface2.darker (0.05f)
                          : (over ? theme::colours::accent.withAlpha (0.14f)
                                  : theme::colours::surface1));
        g.fillPath (p);
        g.setColour (over ? theme::colours::accent : theme::colours::border);
        g.strokePath (p, juce::PathStrokeType (1.0f));

        const float bw = bounds.getWidth() * 0.50f;
        const float bh = bounds.getHeight() * 0.32f;
        const float bx = bounds.getCentreX() - bw * 0.5f;
        const float by = bounds.getBottom() - bh - bounds.getHeight() * 0.16f;
        juce::Path body;
        body.addRoundedRectangle (bx, by, bw, bh, 1.5f);
        g.setColour (theme::colours::accent);
        g.fillPath (body);
        juce::Path shackle;
        const float r = bw * 0.30f;
        shackle.addCentredArc (bounds.getCentreX(), by, r, r, 0.0f,
                               juce::MathConstants<float>::pi, 0.0f, true);
        g.strokePath (shackle, juce::PathStrokeType (1.3f,
                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

// Circular power button reflecting the (inverted) bypass parameter.
class PowerButton final : public juce::ToggleButton
{
public:
    PowerButton() { setButtonText ({}); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().withSizeKeepingCentre (26.0f, 26.0f);
        const bool active = ! getToggleState();
        const juce::Colour fill = active ? theme::colours::surface1 : theme::colours::surface2;
        const juce::Colour ink = active ? theme::colours::accent : theme::colours::textTertiary;

        juce::Path bg;
        bg.addEllipse (bounds);
        g.setColour (fill);
        g.fillPath (bg);
        g.setColour (active ? theme::colours::accent : theme::colours::border);
        g.strokePath (bg, juce::PathStrokeType (1.0f));

        auto body = bounds.reduced (6.5f);
        juce::Path ring;
        ring.addCentredArc (body.getCentreX(), body.getCentreY(), body.getWidth() * 0.5f,
                            body.getHeight() * 0.5f, 0.0f,
                            juce::MathConstants<float>::pi * 0.6f,
                            juce::MathConstants<float>::pi * 2.4f, true);
        juce::Path stroked;
        juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createStrokedPath (stroked, ring);
        g.setColour (ink);
        g.fillPath (stroked);
        g.drawLine ({ body.getCentreX(), body.getY() - 1.5f, body.getCentreX(),
                      body.getCentreY() - 1.0f },
                    1.7f);
    }
};

class AkiTiltAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit AkiTiltAudioProcessorEditor (AkiTiltAudioProcessor&);
    ~AkiTiltAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void saveCurrentSlot();
    void loadPreset (int index);
    void refreshPresets (int selectedIndex);
    void applyPresetEntry (int index);
    void openSaveDialog();
    void refreshAutoMode();

    // Design-space (780x540) to actual-size mapping shared by paint() and
    // resized().
    float scale() const { return (float) getWidth() / 780.0f; }
    juce::Rectangle<float> scaled (float x, float y, float w, float h) const;

    AkiTiltAudioProcessor& processorRef;
    AkiLookAndFeel lookAndFeel;

    XYPad pad;
    PresetBar presetBar;

    juce::ToggleButton limiterButton { "Limit" };
    PowerButton bypassButton;

    MintKnob formantKnob, bandsKnob, pitchKnob, tiltKnob, mixKnob;
    MintKnob widthKnob, airKnob, toneKnob, outKnob;

    LevelMeter inMeter { "IN" }, outMeter { "OUT" };
    juce::ComboBox fftBox;
    juce::Label statusLabel;
    juce::ToggleButton autoFormantButton { "A" };

    juce::AudioProcessorValueTreeState::ButtonAttachment limiterAttachment,
        bypassAttachment, autoFormantAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fftAttachment;

    std::atomic<float>* formantParam = nullptr;
    std::atomic<float>* pitchParam = nullptr;
    std::atomic<float>* bandsParam = nullptr;
    std::atomic<float>* tiltParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* airParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* outParam = nullptr;
    juce::uint32 lastPaintedFrame = 0;
    bool lastAutoVisual = false;
    juce::String lastStatusText;

    int currentPresetIndex = 0;
    char activeSlot = 'A';
    juce::ValueTree slotTrees[2];

    juce::ToggleButton xLock { "X" }, yLock { "Y" };
    LockBadgeButton formantLockBadge, pitchLockBadge;
    juce::TooltipWindow tooltipWindow { this };
    std::vector<aki::presets::PresetEntry> presetEntries;
    std::unique_ptr<juce::AlertWindow> saveWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AkiTiltAudioProcessorEditor)
};

} // namespace aki::ui
