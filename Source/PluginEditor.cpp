#include "PluginEditor.h"

namespace aki::ui
{

using namespace aki::theme;
using aki::params::airAmount;
using aki::params::airTone;
using aki::params::bands;
using aki::params::dryWet;
using aki::params::fftSize;
using aki::params::formantMode;
using aki::params::formantShift;
using aki::params::limiter;
using aki::params::outputGain;
using aki::params::pitchShift;
using aki::params::tilt;
using aki::params::width;

AkiTiltAudioProcessorEditor::AkiTiltAudioProcessorEditor (AkiTiltAudioProcessor& p)
    : AudioProcessorEditor (p),
      processorRef (p),
      formantKnob (p.getParameterTree(), formantShift, "Formant", MintKnob::Format::Cents),
      bandsKnob (p.getParameterTree(), aki::params::bands, "Bands", MintKnob::Format::Bands),
      pitchKnob (p.getParameterTree(), pitchShift, "Pitch", MintKnob::Format::Semitones),
      tiltKnob (p.getParameterTree(), tilt, "Tilt", MintKnob::Format::Decibels),
      mixKnob (p.getParameterTree(), dryWet, "Dry/Wet", MintKnob::Format::Percent),
      widthKnob (p.getParameterTree(), width, "Width", MintKnob::Format::Percent),
      airKnob (p.getParameterTree(), airAmount, "Air", MintKnob::Format::Percent),
      toneKnob (p.getParameterTree(), airTone, "Tone", MintKnob::Format::Percent),
      outKnob (p.getParameterTree(), outputGain, "Out", MintKnob::Format::Decibels),
      limiterAttachment (p.getParameterTree(), limiter, limiterButton),
      bypassAttachment (p.getParameterTree(), aki::params::bypass, bypassButton),
      autoFormantAttachment (p.getParameterTree(), formantMode, autoFormantButton)
{
    setLookAndFeel (&lookAndFeel);

    formantParam = p.getParameterTree().getRawParameterValue (formantShift);
    pitchParam = p.getParameterTree().getRawParameterValue (pitchShift);
    bandsParam = p.getParameterTree().getRawParameterValue (aki::params::bands);
    tiltParam = p.getParameterTree().getRawParameterValue (tilt);
    widthParam = p.getParameterTree().getRawParameterValue (width);
    airParam = p.getParameterTree().getRawParameterValue (airAmount);
    toneParam = p.getParameterTree().getRawParameterValue (airTone);
    outParam = p.getParameterTree().getRawParameterValue (outputGain);

    pad.onChange = [this] (float cents, float st)
    {
        if (auto* formant = processorRef.getParameterTree().getParameter (formantShift))
            formant->setValueNotifyingHost (formant->convertTo0to1 ((float) cents));
        if (auto* pitch = processorRef.getParameterTree().getParameter (pitchShift))
            pitch->setValueNotifyingHost (pitch->convertTo0to1 ((float) st));
    };
    addAndMakeVisible (pad);

    refreshPresets (0);
    presetBar.onPresetChosen = [this] (int index) { applyPresetEntry (index); };
    presetBar.onPrev = [this]()
    {
        const int n = (int) presetEntries.size();
        applyPresetEntry ((currentPresetIndex - 1 + n) % n);
    };
    presetBar.onNext = [this]()
    {
        const int n = (int) presetEntries.size();
        applyPresetEntry ((currentPresetIndex + 1) % n);
    };
    presetBar.onSave = [this]() { openSaveDialog(); };
    presetBar.onOpenFolder = [this]()
    {
        auto dir = aki::presets::userPresetDirectory();
        dir.createDirectory();
        dir.startAsProcess();
    };
    presetBar.onSlot = [this] (char slot)
    {
        if (slot == activeSlot)
            return;
        saveCurrentSlot();
        activeSlot = slot;
        auto& tree = processorRef.getParameterTree();
        tree.replaceState (slotTrees[slot == 'A' ? 0 : 1]);
    };
    addAndMakeVisible (presetBar);

    addAndMakeVisible (limiterButton);
    addAndMakeVisible (bypassButton);

    // FFT size selector: the items must exist before the attachment syncs,
    // so the attachment is created here instead of the init list.
    for (int i = 0; i < aki::dsp::FormantShifterEngine::numFftOrders; ++i)
        fftBox.addItem (juce::String (1 << (aki::dsp::FormantShifterEngine::minFftOrder + i)), i + 1);
    fftBox.setSelectedId (aki::dsp::FormantShifterEngine::defaultFftOrder
                              - aki::dsp::FormantShifterEngine::minFftOrder + 1,
                          juce::dontSendNotification);
    fftBox.setJustificationType (juce::Justification::centredLeft);
    fftBox.setColour (juce::ComboBox::textColourId, colours::textPrimary);
    fftBox.setColour (juce::ComboBox::backgroundColourId, colours::surface2);
    fftBox.setColour (juce::ComboBox::outlineColourId, colours::border);
    fftBox.setColour (juce::ComboBox::arrowColourId, colours::textSecondary);
    fftBox.setTooltip ("Analysis window size - smaller = lower latency and tighter"
                       " tracking, larger = smoother formants. Changing it briefly"
                       " silences the wet path.");
    addAndMakeVisible (fftBox);
    fftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.getParameterTree(), fftSize, fftBox);

    autoFormantButton.setTooltip ("Auto formant - the shift follows the detected"
                                  " input pitch; the knob becomes a fine offset");
    addAndMakeVisible (autoFormantButton);
    autoFormantButton.onStateChange = [this] { refreshAutoMode(); };

    for (auto* b : { &xLock, &yLock })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    xLock.onStateChange = [this]()
    {
        pad.setLockX (xLock.getToggleState());
        // A locked axis also locks its knob; the padlock badge releases it.
        const bool locked = xLock.getToggleState();
        formantKnob.setEnabled (! locked);
        formantLockBadge.setVisible (locked);
        formantKnob.setTooltip (locked ? "Locked - press the padlock to unlock" : "");
    };
    yLock.onStateChange = [this]()
    {
        pad.setLockY (yLock.getToggleState());
        const bool locked = yLock.getToggleState();
        pitchKnob.setEnabled (! locked);
        pitchLockBadge.setVisible (locked);
        pitchKnob.setTooltip (locked ? "Locked - press the padlock to unlock" : "");
    };
    for (auto* badge : { &formantLockBadge, &pitchLockBadge })
    {
        badge->onClick = [this, badge]()
        {
            (badge == &formantLockBadge ? xLock : yLock)
                .setToggleState (false, juce::sendNotificationSync);
        };
        badge->setTooltip ("Release the axis lock");
    }
    xLock.setTooltip ("Lock the formant axis (hold Alt to invert)");
    yLock.setTooltip ("Lock the pitch axis (hold Alt to invert)");
    presetBar.save.setTooltip ("Save the current settings as a user preset");
    presetBar.openFolder.setTooltip ("Open the user preset folder");

    for (auto* knob : { &formantKnob, &bandsKnob, &pitchKnob, &tiltKnob, &mixKnob,
                        &widthKnob, &airKnob, &toneKnob, &outKnob })
        addAndMakeVisible (*knob);

    // Overlay badges must sit above the (disabled) knobs to catch clicks.
    addChildComponent (formantLockBadge);
    addChildComponent (pitchLockBadge);

    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    statusLabel.setText ({}, juce::dontSendNotification);
    statusLabel.setFont (mono (10.0f));
    statusLabel.setColour (juce::Label::textColourId, colours::textSecondary);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    slotTrees[0] = processorRef.getParameterTree().copyState();
    slotTrees[1] = slotTrees[0];

    setSize (780, 540);
    setResizable (true, true);
    setResizeLimits (620, 429, 1560, 1080);
    getConstrainer()->setFixedAspectRatio (780.0 / 540.0);
    startTimerHz (30);
}

AkiTiltAudioProcessorEditor::~AkiTiltAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void AkiTiltAudioProcessorEditor::refreshAutoMode()
{
    const bool autoOn = autoFormantButton.getToggleState();
    if (autoOn == lastAutoVisual)
        return;
    lastAutoVisual = autoOn;
    pad.setAutoMode (autoOn);
    // While the detector owns the X axis, the manual lock controls are
    // unavailable (dimmed, not hidden).
    xLock.setEnabled (! autoOn);
    formantLockBadge.setEnabled (! autoOn);
    xLock.setTooltip (autoOn ? "Auto formant drives the X axis"
                             : "Lock the formant axis (hold Alt to invert)");
    formantLockBadge.setTooltip (autoOn ? "Auto formant drives this axis"
                                        : "Release the axis lock");
    formantKnob.setTooltip (autoOn
                                ? "Formant offset added on top of the auto-tracked shift"
                                : "");
}

void AkiTiltAudioProcessorEditor::timerCallback()
{
    refreshAutoMode();
    if (autoFormantButton.getToggleState())
        pad.setAutoCursorCents (processorRef.effectiveFormantCents.load());

    if (! pad.isDragging())
        pad.setCursor (formantParam->load(), pitchParam->load());

    // Repaint the panel only when the engine published a new frame and it
    // carries energy (silence gates the repaint entirely).
    const juce::uint32 frame = processorRef.engine.getFrameCounter();
    if (frame != lastPaintedFrame)
    {
        lastPaintedFrame = frame;
        float spectrum[aki::dsp::FormantShifterEngine::maxBins];
        float envelope[aki::dsp::FormantShifterEngine::maxBins];
        const int bins = processorRef.engine.getPublishedBins();
        processorRef.engine.copyLatestSpectrum (spectrum, bins);
        processorRef.engine.copyLatestEnvelope (envelope, bins);
        pad.setSpectrum (spectrum, envelope, bins,
                         processorRef.getSampleRate() > 0.0 ? processorRef.getSampleRate() : 48000.0,
                         processorRef.engine.getPublishedWidth(),
                         processorRef.engine.getFftSize());
    }
    pad.setOverlay (bandsParam->load(), tiltParam->load(), widthParam->load(),
                    airParam->load(), toneParam->load(), outParam->load());

    // Live status: the current STFT size and its round-trip time.
    const double sr = processorRef.getSampleRate() > 0.0 ? processorRef.getSampleRate() : 48000.0;
    const int fftNow = processorRef.engine.getFftSize();
    juce::String status;
    status << "STFT " << fftNow << "\n"
           << juce::String (fftNow / sr * 1000.0, 1) << " ms round-trip";
    if (status != lastStatusText)
    {
        lastStatusText = status;
        statusLabel.setText (status, juce::dontSendNotification);
    }

    inMeter.setLevelDb (processorRef.inPeakDb.load());
    outMeter.setLevelDb (processorRef.outPeakDb.load());
}

void AkiTiltAudioProcessorEditor::saveCurrentSlot()
{
    slotTrees[activeSlot == 'A' ? 0 : 1] = processorRef.getParameterTree().copyState();
}

void AkiTiltAudioProcessorEditor::refreshPresets (int selectedIndex)
{
    presetEntries = aki::presets::scanPresets();
    juce::StringArray names;
    for (const auto& entry : presetEntries)
        names.add (entry.name);
    const int factoryCount = (int) aki::presets::getAll().size();
    presetBar.setItems (names, factoryCount, juce::jlimit (0, names.size() - 1, selectedIndex));
}

void AkiTiltAudioProcessorEditor::applyPresetEntry (int index)
{
    if (index < 0 || index >= (int) presetEntries.size())
        return;
    const auto& entry = presetEntries[(size_t) index];
    if (entry.isFactory)
        aki::presets::apply (processorRef.getParameterTree(), entry.factoryIndex);
    else
        aki::presets::applyFile (processorRef.getParameterTree(), entry.file);
    currentPresetIndex = index;
    presetBar.setSelectedIndex (index);
}

void AkiTiltAudioProcessorEditor::loadPreset (int index)
{
    applyPresetEntry (index);
}

void AkiTiltAudioProcessorEditor::openSaveDialog()
{
    if (saveWindow != nullptr)
        return;
    saveWindow.reset (new juce::AlertWindow ("Save preset", "Preset name:",
                                             juce::AlertWindow::NoIcon));
    saveWindow->addTextEditor ("name", "My preset");
    saveWindow->addButton ("Save", 1);
    saveWindow->addButton ("Cancel", 0);
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveWindow != nullptr)
        {
            const auto name = saveWindow->getTextEditorContents ("name");
            if (aki::presets::saveUserPreset (processorRef.getParameterTree(), name))
            {
                const auto clean = aki::presets::sanitiseName (name);
                int select = 0;
                auto entries = aki::presets::scanPresets();
                for (int i = 0; i < (int) entries.size(); ++i)
                    if (! entries[(size_t) i].isFactory && entries[(size_t) i].name == clean)
                        select = i;
                refreshPresets (select);
                applyPresetEntry (select);
            }
        }
        juce::MessageManager::callAsync ([this] { saveWindow.reset(); });
    }), false);
}

juce::Rectangle<float> AkiTiltAudioProcessorEditor::scaled (float x, float y, float w, float h) const
{
    const float s = scale();
    return { x * s, y * s, w * s, h * s };
}

void AkiTiltAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (colours::bgBase);
    g.fillAll();

    // Subtle aurora tints, ramp-only colours at low opacity.
    juce::ColourGradient aurora1 (colours::gradB.withAlpha (0.10f), bounds.getWidth() * 0.12f,
                                  -40.0f, juce::Colour (0x00000000), bounds.getWidth() * 0.12f,
                                  bounds.getHeight() * 0.7f, true);
    g.setFillType (juce::FillType (aurora1));
    g.fillAll();
    juce::ColourGradient aurora2 (colours::gradA.withAlpha (0.07f), bounds.getWidth() * 0.92f,
                                  bounds.getHeight() + 40.0f, juce::Colour (0x00000000),
                                  bounds.getWidth() * 0.92f, bounds.getHeight() * 0.35f, true);
    g.setFillType (juce::FillType (aurora2));
    g.fillAll();

    auto card = [&] (juce::Rectangle<float> r, juce::Colour fill, float radius, bool shadow)
    { AkiLookAndFeel::drawCard (g, r, fill, radius, shadow); };

    card (scaled (18.0f, 14.0f, 744.0f, 58.0f), colours::surface1, 24.0f, true);
    card (scaled (518.0f, 82.0f, 244.0f, 300.0f), colours::surface1, 24.0f, true);
    card (scaled (18.0f, 394.0f, 744.0f, 96.0f), colours::surface1, 24.0f, true);

    g.setColour (colours::textPrimary);
    g.setFont (display (23.0f * scale()));
    g.drawText ("AkiTilt", scaled (24.0f, 20.0f, 220.0f, 30.0f), juce::Justification::centredLeft);

    g.setColour (colours::textSecondary);
    g.setFont (sansSemiBold (9.5f * scale()));
    g.drawText ("FORMANT & TEXTURE MORPH  -  FOR BOTANICA MAKERS",
                scaled (25.0f, 50.0f, 320.0f, 14.0f), juce::Justification::centredLeft);

    g.setColour (colours::textSecondary);
    g.setFont (mono (10.5f * scale()));
    g.drawText ("v2.0.0  -  Akiro", scaled (400.0f, 502.0f, 336.0f, 16.0f),
                juce::Justification::centredRight);
}

void AkiTiltAudioProcessorEditor::resized()
{
    auto rect = [this] (float x, float y, float w, float h)
    { return scaled (x, y, w, h).toNearestInt(); };

    presetBar.setBounds (rect (346, 29, 372, 28));
    bypassButton.setBounds (rect (722, 30, 26, 26));

    pad.setBounds (rect (18, 82, 490, 300));

    limiterButton.setBounds (rect (587, 96, 104, 28));

    xLock.setBounds (rect (448, 310, 20, 15));
    yLock.setBounds (rect (470, 310, 20, 15));
    autoFormantButton.setBounds (rect (426, 310, 20, 15));

    formantLockBadge.setBounds (rect (124, 400, 15, 17));
    pitchLockBadge.setBounds (rect (272, 400, 15, 17));

    widthKnob.setBounds (rect (532, 136, 72, 114));
    airKnob.setBounds (rect (604, 136, 72, 114));
    toneKnob.setBounds (rect (676, 136, 72, 114));

    outKnob.setBounds (rect (532, 256, 72, 114));
    inMeter.setBounds (rect (614, 264, 16, 98));
    outMeter.setBounds (rect (638, 264, 16, 98));
    fftBox.setBounds (rect (662, 264, 92, 24));
    statusLabel.setBounds (rect (662, 292, 92, 70));

    const float cellWidth = 744.0f / 5.0f;
    MintKnob* bottomKnobs[] = { &formantKnob, &pitchKnob, &bandsKnob, &tiltKnob, &mixKnob };
    for (int i = 0; i < 5; ++i)
    {
        const float x = 18.0f + cellWidth * (float) i;
        bottomKnobs[i]->setBounds (rect (x + 24.0f, 398.0f, 100.0f, 88.0f));
    }
}

} // namespace aki::ui
