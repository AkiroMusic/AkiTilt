#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace aki::ui
{

// Hero control on the dark contrast panel: X = formant (-1200..+1200 ct),
// Y = pitch (+12..-12 st, up is higher). Behind the cursor it draws the live
// magnitude spectrum, the peak-region envelope (the curve the Bands knob
// shapes) and the tilt response curve. Repaints are change-gated: a frame
// id, an idle-energy gate and overlay value comparison keep the idle cost
// near zero.
class XYPad final : public juce::Component
{
public:
    static constexpr float maxCents = 1200.0f;
    static constexpr float maxSemitones = 12.0f;

    std::function<void (float formantCents, float pitchSemitones)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setCursor (float formantCents, float pitchSemitones);
    // Post-effect magnitudes + shifted envelope from the same engine frame,
    // plus the band width and FFT size used by that frame (band-boundary
    // ticks and the bin-to-frequency mapping). Returns false when the frame
    // is silent and identical to the last silent frame (no repaint needed).
    bool setSpectrum (const float* magnitudes, const float* envelope, int count,
                      double sampleRateForXAxis, int bandsWidth, int fftSizeUsed);
    void setOverlay (float bands, float tiltDb, float widthPct, float airPct,
                     float tonePct, float outDb);
    bool isDragging() const { return dragging; }

    // Axis locks: a locked axis keeps its value while dragging. Alt-drag
    // inverts the locks for that gesture.
    void setLockX (bool locked) { lockX = locked; }
    void setLockY (bool locked) { lockY = locked; }
    bool isLockedX() const { return lockX; }
    bool isLockedY() const { return lockY; }

    // Auto formant: the X axis is driven by the pitch detector. Manual X
    // dragging is ignored and the cursor shows the effective (auto) value.
    void setAutoMode (bool autoEnabled);
    void setAutoCursorCents (float cents);

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void updateFromMouseEvent (const juce::MouseEvent&);
    float binToNormX (int bin) const;
    float freqToNormX (float hz) const;
    float magnitudeToNormY (float magnitude) const;

    float cursorCents = 0.0f;
    float cursorSemitones = 0.0f;
    bool dragging = false;

    float overlayBands = 10.0f, overlayTilt = 0.0f, overlayWidth = 100.0f;
    float overlayAir = 0.0f, overlayTone = 50.0f, overlayOut = 0.0f;

    std::vector<float> spectrum;
    std::vector<float> envelopeCurve;
    int bandWidth = 10;
    int binningFftSize = 2048;
    bool spectrumIdle = true;
    double referenceSampleRate = 48000.0;

    bool lockX = false, lockY = false;
    bool autoX = false;
    float autoCursorCents = 0.0f;
    juce::Point<float> dragAnchorPos;
    float dragAnchorCents = 0.0f, dragAnchorSemitones = 0.0f;
    float dragStartCents = 0.0f, dragStartSemitones = 0.0f;
};

} // namespace aki::ui
