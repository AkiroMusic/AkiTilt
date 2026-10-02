#include "AkiLookAndFeel.h"

#include "Theme.h"

namespace aki::ui
{

using namespace aki::theme;

void AkiLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPosProportional, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.72f;
    const juce::Point<float> centre = bounds.getCentre();
    const float radius = diameter * 0.5f;
    const float angle = rotaryStartAngle
                      + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const float arcThickness = 5.0f;

    // Track arc on a recessed surface-2 well.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    juce::Path strokedTrack;
    juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded)
        .createStrokedPath (strokedTrack, track);
    g.setColour (colours::surface2);
    g.fillPath (strokedTrack);
    g.setColour (colours::innerShade);
    g.strokePath (track, juce::PathStrokeType (arcThickness * 2.0f + 1.0f));
    g.setColour (colours::surface1);
    g.strokePath (track, juce::PathStrokeType (arcThickness * 2.0f - 1.0f));

    // Value arc, ramp-tinted (gradA -> gradB horizontal gradient).
    if (sliderPosProportional > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             rotaryStartAngle, angle, true);
        juce::Path strokedValue;
        juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                              juce::PathStrokeType::rounded)
            .createStrokedPath (strokedValue, value);
        juce::ColourGradient fill (colours::gradA,
                                   centre.x - radius, centre.y,
                                   colours::gradB,
                                   centre.x + radius, centre.y, false);
        g.setFillType (juce::FillType (fill));
        g.fillPath (strokedValue);
    }

    // Pointer: deep ink-green line + center dot on the surface.
    juce::Path pointer;
    const float pointerLength = radius - arcThickness - 4.0f;
    pointer.startNewSubPath (centre.x + std::sin (angle) * 3.0f,
                             centre.y - std::cos (angle) * 3.0f);
    pointer.lineTo (centre.x + std::sin (angle) * pointerLength,
                    centre.y - std::cos (angle) * pointerLength);
    g.setColour (colours::textPrimary);
    g.strokePath (pointer, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    g.setColour (colours::accent);
    g.fillEllipse (centre.x - 2.6f, centre.y - 2.6f, 5.2f, 5.2f);
}

void AkiLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                       bool highlighted, bool)
{
    // Disabled (not locked — genuinely unavailable) controls fade back.
    if (! button.isEnabled())
        g.setOpacity (0.4f);

    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    const juce::Colour accent = on ? colours::accent : colours::textSecondary;
    juce::Colour fill = on ? colours::accent.withAlpha (highlighted ? 0.16f : 0.12f)
                           : colours::surface2;
    if (! on && highlighted)
        fill = colours::surface2.brighter (0.04f);

    const float pillHeight = juce::jmin (28.0f, bounds.getHeight());
    auto pill = bounds.withHeight (pillHeight).withCentre (bounds.getCentre());
    juce::Path p;
    p.addRoundedRectangle (pill, 10.0f);
    g.setColour (fill);
    g.fillPath (p);
    g.setColour (on ? colours::accent : colours::border);
    g.strokePath (p, juce::PathStrokeType (1.0f));

    const float dot = 6.0f;
    const bool narrow = pill.getWidth() < 44.0f;
    if (narrow)
    {
        g.setColour (on ? colours::accent : colours::textSecondary);
        g.setFont (sansSemiBold (juce::jmin (13.0f, pillHeight - 4.0f)));
        g.drawText (button.getButtonText().toUpperCase(), pill, juce::Justification::centred);
    }
    else
    {
        g.setColour (on ? colours::accent : colours::textTertiary);
        g.fillEllipse (pill.getX() + 12.0f, pill.getCentreY() - dot * 0.5f, dot, dot);

        g.setColour (on ? colours::accent : colours::textSecondary);
        g.setFont (sansSemiBold (13.0f));
        g.drawText (button.getButtonText().toUpperCase(),
                    juce::Rectangle<float> (pill.getX() + 24.0f, pill.getY(),
                                            pill.getWidth() - 30.0f, pill.getHeight()),
                    juce::Justification::centredLeft);
    }
}

void AkiLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                           const juce::Colour& backgroundColour,
                                           bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    juce::ignoreUnused (backgroundColour);
    const bool on = button.isToggleable() && button.getToggleState();
    juce::Path p;
    p.addRoundedRectangle (bounds, 10.0f);
    const juce::Colour base = on ? colours::accent.withAlpha (0.12f) : colours::surface2;
    g.setColour (down ? base.darker (0.05f) : (highlighted ? base.brighter (0.04f) : base));
    g.fillPath (p);
    g.setColour (on ? colours::accent : colours::border);
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

void AkiLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                     bool, bool)
{
    g.setColour (button.isToggleable() && button.getToggleState() ? colours::accent
                                                                  : colours::textSecondary);
    g.setFont (sansSemiBold (13.0f));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

void AkiLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                   int, int, int, int, juce::ComboBox&)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    juce::Path p;
    p.addRoundedRectangle (bounds, 10.0f);
    g.setColour (colours::surface2);
    g.fillPath (p);
    g.setColour (colours::border);
    g.strokePath (p, juce::PathStrokeType (1.0f));

    // Chevron in mono-medium style.
    const float cx = bounds.getRight() - 16.0f;
    const float cy = bounds.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.5f);
    chevron.lineTo (cx, cy + 2.5f);
    chevron.lineTo (cx + 4.0f, cy - 2.5f);
    g.setColour (colours::textSecondary);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void AkiLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                                         const juce::PopupMenu::Options&)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    juce::Path p;
    p.addRoundedRectangle (bounds, 10.0f);
    g.setColour (colours::surface1);
    g.fillPath (p);
    g.setColour (colours::border);
    g.strokePath (p, juce::PathStrokeType (1.0f));
    g.setColour (colours::bezelInner);
    juce::Path inner;
    inner.addRoundedRectangle (bounds.reduced (4.0f), 7.0f);
    g.strokePath (inner, juce::PathStrokeType (1.0f));
}

void AkiLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                        bool isSeparator, bool isActive, bool isHighlighted,
                                        bool isTicked, bool, const juce::String& text,
                                        const juce::String&, const juce::Drawable*,
                                        const juce::Colour*)
{
    auto bounds = area.toFloat().reduced (4.0f, 1.0f);
    if (isHighlighted && isActive)
    {
        juce::Path p;
        p.addRoundedRectangle (bounds, 7.0f);
        g.setColour (colours::accent.withAlpha (0.10f));
        g.fillPath (p);
    }
    if (isSeparator)
    {
        g.setColour (colours::border);
        g.fillRect (bounds.withHeight (1.0f));
        return;
    }
    g.setColour (isActive ? (isHighlighted ? colours::accent : colours::textPrimary)
                          : colours::textTertiary);
    g.setFont (sansMedium (13.0f));
    g.drawText (text, bounds.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
    if (isTicked)
    {
        g.setColour (colours::accent);
        g.fillEllipse (bounds.getRight() - 10.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
    }
}

void AkiLookAndFeel::drawCard (juce::Graphics& g, juce::Rectangle<float> bounds,
                               juce::Colour fill, float cornerRadius, bool dropShadow)
{
    if (dropShadow)
    {
        juce::Path shadowPath;
        shadowPath.addRoundedRectangle (bounds, cornerRadius);
        juce::DropShadow shadow (colours::shadowTint, 14, { 0, 5 });
        shadow.drawForPath (g, shadowPath);
    }

    juce::Path p;
    p.addRoundedRectangle (bounds, cornerRadius);
    g.setColour (fill);
    g.fillPath (p);
    g.setColour (fill == colours::contrast ? colours::onContrastHair.withAlpha (0.10f)
                                           : colours::border);
    g.strokePath (p, juce::PathStrokeType (1.0f));

    // Inner recessed hairline at 5px inset (double bezel).
    juce::Path inner;
    inner.addRoundedRectangle (bounds.reduced (5.0f), juce::jmax (cornerRadius - 4.0f, 4.0f));
    g.setColour (fill == colours::contrast ? colours::onContrastHair : colours::bezelInner);
    g.strokePath (inner, juce::PathStrokeType (1.0f));
}

} // namespace aki::ui
