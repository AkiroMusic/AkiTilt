#include "XYPad.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include "Theme.h"

namespace aki::ui
{

using namespace aki::theme;

namespace
{
float centsToNormX (float cents)
{
    return (juce::jlimit (-XYPad::maxCents, XYPad::maxCents, cents) + XYPad::maxCents)
         / (2.0f * XYPad::maxCents);
}

float semitonesToNormY (float st)
{
    return (XYPad::maxSemitones - juce::jlimit (-XYPad::maxSemitones, XYPad::maxSemitones, st))
         / (2.0f * XYPad::maxSemitones);
}
} // namespace

void XYPad::setCursor (float formantCents, float pitchSemitones)
{
    if (juce::approximatelyEqual (cursorCents, formantCents)
        && juce::approximatelyEqual (cursorSemitones, pitchSemitones))
        return;
    cursorCents = formantCents;
    cursorSemitones = pitchSemitones;
    repaint();
}

bool XYPad::setSpectrum (const float* magnitudes, const float* envelope, int count,
                         double sampleRateForXAxis, int bandsWidth, int fftSizeUsed)
{
    if (count <= 0)
        return false;
    float energy = 0.0f;
    for (int i = 0; i < count; ++i)
        energy += magnitudes[i];
    const bool idle = energy < 1.0e-3f;
    if (idle && spectrumIdle && fftSizeUsed == binningFftSize)
        return false;
    spectrumIdle = idle;
    referenceSampleRate = sampleRateForXAxis;
    binningFftSize = juce::jmax (64, fftSizeUsed);
    bandWidth = juce::jlimit (2, binningFftSize / 4, bandsWidth);
    spectrum.assign (magnitudes, magnitudes + count);
    envelopeCurve.assign (envelope, envelope + count);
    repaint();
    return true;
}

void XYPad::setAutoMode (bool autoEnabled)
{
    if (autoX == autoEnabled)
        return;
    autoX = autoEnabled;
    repaint();
}

void XYPad::setAutoCursorCents (float cents)
{
    if (! autoX || juce::approximatelyEqual (autoCursorCents, cents))
        return;
    autoCursorCents = juce::jlimit (-maxCents, maxCents, cents);
    repaint();
}

void XYPad::setOverlay (float bands, float tiltDb, float widthPct, float airPct,
                        float tonePct, float outDb)
{
    auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-4f; };
    if (same (overlayBands, bands) && same (overlayTilt, tiltDb) && same (overlayWidth, widthPct)
        && same (overlayAir, airPct) && same (overlayTone, tonePct) && same (overlayOut, outDb))
        return;
    overlayBands = bands;
    overlayTilt = tiltDb;
    overlayWidth = widthPct;
    overlayAir = airPct;
    overlayTone = tonePct;
    overlayOut = outDb;
    repaint();
}

float XYPad::binToNormX (int bin) const
{
    const float binHz = (float) juce::jmax (1, bin) * (float) (referenceSampleRate / (double) binningFftSize);
    return freqToNormX (binHz);
}

float XYPad::freqToNormX (float hz) const
{
    constexpr float minHz = 20.0f, maxHz = 20000.0f;
    const float t = (std::log (juce::jlimit (minHz, maxHz, hz)) - std::log (minHz))
                  / (std::log (maxHz) - std::log (minHz));
    return juce::jlimit (0.0f, 1.0f, t);
}

float XYPad::magnitudeToNormY (float magnitude) const
{
    const float db = juce::Decibels::gainToDecibels (juce::jmax (magnitude, 1.0e-9f), -90.0f);
    return juce::jlimit (0.0f, 1.0f, (db + 78.0f) / 138.0f);
}

void XYPad::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::Path panel;
    panel.addRoundedRectangle (bounds, 26.0f);
    g.saveState();
    g.reduceClipRegion (panel);
    g.setColour (colours::contrast);
    g.fillAll();

    // Tone tint: the panel is green at the default Tone (50 %), washes
    // brown as low frequencies dominate and grass-green as highs do.
    {
        const float d = juce::jlimit (-0.5f, 0.5f, overlayTone / 100.0f - 0.5f);
        const juce::Colour toneColour = d < 0.0f
            ? juce::Colour (0xff8F6B4A).interpolatedWith (colours::contrast, 1.0f + d * 2.0f)
            : colours::gradB.interpolatedWith (colours::contrast, 1.0f - d * 2.0f);
        g.setColour (toneColour.withAlpha ((std::abs (d) * 2.0f) * 0.22f));
        g.fillAll();
    }

    const auto area = bounds.reduced (14.0f);

    // EQ-style frequency grid: octave-ish columns (100 Hz .. 10 kHz bold)
    // and dB rows, market-EQ density.
    const float h = area.getHeight();
    auto dbToY = [area, h] (float db) { return area.getBottom() - ((db + 78.0f) / 138.0f) * h; };
    for (float fHz : { 50.0f, 200.0f, 500.0f, 2000.0f, 5000.0f })
    {
        const float x = area.getX() + freqToNormX (fHz) * area.getWidth();
        g.setColour (colours::onContrastHair.withAlpha (0.09f));
        g.fillRect (x - 0.5f, area.getY(), 1.0f, area.getHeight());
    }
    for (float fHz : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = area.getX() + freqToNormX (fHz) * area.getWidth();
        g.setColour (colours::onContrastHair.withAlpha (0.20f));
        g.fillRect (x - 0.5f, area.getY(), 1.0f, area.getHeight());
    }
    for (float db : { -60.0f, -40.0f, 20.0f, 40.0f })
    {
        g.setColour (colours::onContrastHair.withAlpha (0.09f));
        g.fillRect (area.getX(), dbToY (db) - 0.5f, area.getWidth(), 1.0f);
    }
    g.setColour (colours::onContrastHair.withAlpha (0.16f));
    g.fillRect (area.getX(), dbToY (0.0f) - 0.5f, area.getWidth(), 1.0f);
    // Zero axes (formant = 0, pitch = 0) slightly brighter.
    const float zeroX = area.getX() + area.getWidth() * 0.5f;
    const float zeroY = area.getCentreY();
    g.setColour (colours::onContrastHair.withAlpha (0.20f));
    g.fillRect (zeroX - 0.5f, area.getY(), 1.0f, area.getHeight());
    g.fillRect (area.getX(), zeroY - 0.5f, area.getWidth(), 1.0f);
    // Axis captions for the grid.
    g.setColour (colours::onContrastWeak);
    g.setFont (mono (9.5f));
    g.drawText ("100", juce::Rectangle<float> (area.getX() + freqToNormX (100.0f) * area.getWidth() - 24.0f,
                                               area.getBottom() - 31.0f, 48.0f, 12.0f),
                juce::Justification::centred);
    g.drawText ("1k", juce::Rectangle<float> (area.getX() + freqToNormX (1000.0f) * area.getWidth() - 24.0f,
                                              area.getBottom() - 31.0f, 48.0f, 12.0f),
                juce::Justification::centred);
    g.drawText ("10k", juce::Rectangle<float> (area.getX() + freqToNormX (10000.0f) * area.getWidth() - 24.0f,
                                               area.getBottom() - 31.0f, 48.0f, 12.0f),
                juce::Justification::centred);
    g.drawText ("0 dB", juce::Rectangle<float> (area.getX() + 2.0f, dbToY (0.0f) + 2.0f, 40.0f, 11.0f),
                juce::Justification::topLeft);

    // Band-boundary ticks rising from the bottom edge: the analysis regions
    // the Bands knob controls (density capped so fine settings stay
    // readable).
    if (! spectrum.empty())
    {
        const int envBins = binningFftSize / 2;
        const int regionCount = (envBins + bandWidth - 1) / bandWidth;
        const int step = juce::jmax (1, regionCount / 64);
        g.setColour (colours::gradC.withAlpha (0.45f));
        for (int r = 0; r <= regionCount; r += step)
        {
            const int bin = juce::jmin (r * bandWidth, envBins);
            const float x = area.getX() + binToNormX (bin) * area.getWidth();
            g.fillRect (x - 0.5f, area.getBottom() - 40.0f, 1.0f, 40.0f);
        }
    }

    // Tilt response curve spanning the full pad width (background layer).
    // Positive tilt = brighter = high shelf up: the right (high-frequency)
    // end of the curve rises.
    if (std::abs (overlayTilt) > 0.01f)
    {
        const float amp = (overlayTilt / 12.0f) * 0.20f * area.getHeight();
        juce::Path tiltPath;
        constexpr int steps = 64;
        for (int i = 0; i <= steps; ++i)
        {
            const float u = (float) i / (float) steps;
            const float x = area.getX() + u * area.getWidth();
            const float y = area.getCentreY() - amp * std::tanh ((u - 0.504f) * 6.0f);
            if (i == 0)
                tiltPath.startNewSubPath (x, y);
            else
                tiltPath.lineTo (x, y);
        }
        juce::Path stroked;
        juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createStrokedPath (stroked, tiltPath);
        g.setFillType (juce::FillType (juce::ColourGradient (
            colours::gradA.withAlpha (0.55f), area.getX(), 0.0f,
            colours::gradC.withAlpha (0.55f), area.getRight(), 0.0f, false)));
        g.fillPath (stroked);
    }

    // Magnitude spectrum, stroked with the ramp.
    if ((int) spectrum.size() >= 4)
    {
        const int step = juce::jmax (1, (int) spectrum.size() / 420);
        juce::Path line;
        bool started = false;
        for (int k = 1; k < (int) spectrum.size(); k += step)
        {
            const float x = area.getX() + binToNormX (k) * area.getWidth();
            const float y = area.getBottom() - magnitudeToNormY (spectrum[(size_t) k]) * area.getHeight();
            if (! started)
            {
                line.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                line.lineTo (x, y);
            }
        }
        juce::Path fill = line;
        fill.lineTo (area.getRight(), area.getBottom());
        fill.lineTo (area.getX(), area.getBottom());
        fill.closeSubPath();
        juce::ColourGradient fillGrad (colours::gradC.withAlpha (0.22f), 0.0f, area.getY(),
                                       colours::gradA.withAlpha (0.04f), 0.0f, area.getBottom(), false);
        g.setFillType (juce::FillType (fillGrad));
        g.fillPath (fill);

        juce::Path stroked;
        juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createStrokedPath (stroked, line);
        juce::ColourGradient strokeGrad (colours::gradA, area.getX(), 0.0f,
                                         colours::gradC, area.getRight(), 0.0f, false);
        g.setFillType (juce::FillType (strokeGrad));
        g.fillPath (stroked);
    }

    // Peak-region envelope (the curve the Bands knob shapes), warm accent.
    if ((int) envelopeCurve.size() >= 4)
    {
        const int step = juce::jmax (1, (int) envelopeCurve.size() / 420);
        juce::Path envLine;
        bool started = false;
        for (int k = 1; k < (int) envelopeCurve.size(); k += step)
        {
            const float x = area.getX() + binToNormX (k) * area.getWidth();
            const float y = area.getBottom()
                          - magnitudeToNormY (envelopeCurve[(size_t) k]) * area.getHeight();
            if (! started)
            {
                envLine.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                envLine.lineTo (x, y);
            }
        }
        juce::Path stroked;
        juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createStrokedPath (stroked, envLine);
        g.setColour (colours::accentTertiary.withAlpha (0.85f));
        g.fillPath (stroked);
    }

    // Cursor with a radial-gradient glow (cheap, no blur pass). In Auto mode
    // the X position shows the detector-driven effective formant.
    const float displayCents = autoX ? autoCursorCents : cursorCents;
    const float cx = area.getX() + centsToNormX (displayCents) * area.getWidth();
    const float cy = area.getY() + semitonesToNormY (cursorSemitones) * area.getHeight();

    g.setColour (colours::onContrastHair.withAlpha (0.16f));
    g.fillRect (area.getX(), cy - 0.5f, area.getWidth(), 1.0f);
    g.fillRect (cx - 0.5f, area.getY(), 1.0f, area.getHeight());

    // Parameter overlay: mono readouts top-left, OUT top-right, legend below it.
    g.setFont (mono (11.5f));
    g.setColour (colours::onContrastStrong);
    auto sign = [] (float v) { return v > 0.0f ? "+" : ""; };
    const float ox = area.getX() + 10.0f;
    float oy = area.getY() + 26.0f;
    auto row = [&] (const juce::String& text)
    {
        g.drawText (text, juce::Rectangle<float> (ox - 4.0f, oy - 7.0f, 150.0f, 15.0f),
                    juce::Justification::centredLeft);
        oy += 17.0f;
    };
    row (juce::String ("BANDS  ") + juce::String (overlayBands, 1));
    row (juce::String ("TILT   ") + sign (overlayTilt) + juce::String (overlayTilt, 1) + " dB");
    row (juce::String ("WIDTH  ") + juce::String (overlayWidth, 0) + " %");
    row (juce::String ("AIR    ") + juce::String (overlayAir, 0) + " %");

    g.drawText (juce::String ("OUT  ") + sign (overlayOut) + juce::String (overlayOut, 1) + " dB",
                juce::Rectangle<float> (area.getRight() - 122.0f, area.getY() + 4.0f, 114.0f, 15.0f),
                juce::Justification::centredRight);

    g.setFont (mono (9.5f));
    {
        const float lx = area.getRight() - 122.0f;
        g.setFillType (juce::FillType (colours::gradC));
        g.fillRect (lx, area.getY() + 24.0f, 12.0f, 1.4f);
        g.drawText ("spectrum", juce::Rectangle<float> (lx + 15.0f, area.getY() + 17.0f, 92.0f, 13.0f),
                    juce::Justification::centredLeft);
        g.setColour (colours::accentTertiary);
        g.fillRect (lx, area.getY() + 37.0f, 12.0f, 1.4f);
        g.drawText ("envelope", juce::Rectangle<float> (lx + 15.0f, area.getY() + 30.0f, 92.0f, 13.0f),
                    juce::Justification::centredLeft);
    }

    // Live cursor readout next to the dot.
    {
        const juce::String text = autoX
            ? juce::String::formatted ("AUTO %+.0f ct   %+.2f st", displayCents, cursorSemitones)
            : juce::String::formatted ("%+.0f ct   %+.2f st", cursorCents, cursorSemitones);
        const float tx = cx > area.getRight() - 190.0f ? cx - 186.0f : cx + 12.0f;
        g.drawText (text, juce::Rectangle<float> (tx, cy - 22.0f, 178.0f, 14.0f),
                    juce::Justification::centredLeft);
    }

    juce::ColourGradient glow (colours::gradB.withAlpha (0.50f), cx, cy,
                               juce::Colours::transparentBlack, cx + 26.0f, cy, true);
    g.setFillType (juce::FillType (glow));
    g.fillEllipse (cx - 26.0f, cy - 26.0f, 52.0f, 52.0f);
    g.setColour (colours::gradC);
    g.fillEllipse (cx - 6.5f, cy - 6.5f, 13.0f, 13.0f);
    g.setColour (colours::contrast);
    g.fillEllipse (cx - 2.2f, cy - 2.2f, 4.4f, 4.4f);

    g.restoreState();

    // Axis captions in mono.
    g.setColour (colours::onContrastStrong);
    g.setFont (mono (11.5f));
    g.drawText ("+12 st", juce::Rectangle<float> (area.getX() + 6.0f, area.getY() + 4.0f,
                                                  80.0f, 15.0f),
                juce::Justification::topLeft);
    g.drawText ("-12 st", juce::Rectangle<float> (area.getX() + 6.0f, area.getBottom() - 34.0f,
                                                  80.0f, 15.0f),
                juce::Justification::bottomLeft);
    g.drawText ("-1200 ct", juce::Rectangle<float> (area.getX(), area.getBottom() - 18.0f,
                                                    90.0f, 15.0f),
                juce::Justification::bottomLeft);
    g.drawText ("+1200 ct", juce::Rectangle<float> (area.getRight() - 96.0f,
                                                    area.getBottom() - 18.0f, 90.0f, 15.0f),
                juce::Justification::bottomRight);

    juce::Path rim;
    rim.addRoundedRectangle (bounds, 26.0f);
    g.setColour (colours::onContrastHair.withAlpha (0.14f));
    g.strokePath (rim, juce::PathStrokeType (1.0f));
}

void XYPad::resized() {}

void XYPad::updateFromMouseEvent (const juce::MouseEvent& e)
{
    const auto area = getLocalBounds().toFloat().reduced (14.0f);
    float nx = juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());
    float ny = juce::jlimit (0.0f, 1.0f, (e.position.y - area.getY()) / area.getHeight());

    // Shift = fine adjustment: one eighth sensitivity around the anchor.
    if (e.mods.isShiftDown())
    {
        const float anchorNx = centsToNormX (dragAnchorCents);
        const float anchorNy = semitonesToNormY (dragAnchorSemitones);
        const float dx = (e.position.x - dragAnchorPos.x) / area.getWidth() / 8.0f;
        const float dy = (e.position.y - dragAnchorPos.y) / area.getHeight() / 8.0f;
        nx = juce::jlimit (0.0f, 1.0f, anchorNx + dx);
        ny = juce::jlimit (0.0f, 1.0f, anchorNy + dy);
    }

    float cents = nx * 2.0f * maxCents - maxCents;
    float st = maxSemitones - ny * 2.0f * maxSemitones;

    // Ctrl = snap to a musical grid (100 ct / 1 semitone).
    if (e.mods.isCtrlDown())
    {
        cents = std::round (cents / 100.0f) * 100.0f;
        st = std::round (st);
    }

    // Axis locks, inverted for this gesture while Alt is held. Auto formant
    // pins X entirely: the detector owns that axis.
    const bool holdX = autoX || lockX != e.mods.isAltDown();
    const bool holdY = lockY != e.mods.isAltDown();
    if (holdX)
        cents = dragStartCents;
    if (holdY)
        st = dragStartSemitones;

    cursorCents = juce::jlimit (-maxCents, maxCents, cents);
    cursorSemitones = juce::jlimit (-maxSemitones, maxSemitones, st);
    if (onChange)
        onChange (cursorCents, cursorSemitones);
    repaint();
}

void XYPad::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    dragAnchorPos = e.position;
    dragAnchorCents = cursorCents;
    dragAnchorSemitones = cursorSemitones;
    dragStartCents = cursorCents;
    dragStartSemitones = cursorSemitones;
    updateFromMouseEvent (e);
}

void XYPad::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        updateFromMouseEvent (e);
}

void XYPad::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
}

void XYPad::mouseDoubleClick (const juce::MouseEvent&)
{
    cursorCents = 0.0f;
    cursorSemitones = 0.0f;
    if (onChange)
        onChange (0.0f, 0.0f);
    repaint();
}

} // namespace aki::ui
