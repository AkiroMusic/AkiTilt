#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace aki::ui
{

// Slim vertical level meter, ramp-tinted below 0 dB and red above it.
// Range -60..+6 dB, dB-linear, with a 0 dB reference line, scale ticks and
// a hold-on clip marker at the top edge.
class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter (const juce::String& captionText) : caption (captionText) {}

    void setLevelDb (float db)
    {
        const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        level = juce::jmax (norm, level * 0.90f);
        clip = db > 0.1f ? 1.0f : juce::jmax (0.0f, clip - 0.035f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().removeFromTop (getHeight() - 14.0f);
        bounds.reduce (1.0f, 1.0f);

        juce::Path track;
        track.addRoundedRectangle (bounds, 3.0f);
        g.setColour (theme::colours::surface2);
        g.fillPath (track);

        // dB scale ticks (0 dB drawn separately, stronger).
        const float h = bounds.getHeight();
        g.setColour (theme::colours::textTertiary.withAlpha (0.28f));
        for (float db : { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f })
        {
            const float y = bounds.getBottom() - ((db + 60.0f) / 66.0f) * h;
            g.fillRect (bounds.getX(), y - 0.5f, bounds.getWidth(), 1.0f);
        }

        const float zeroY = bounds.getBottom() - (60.0f / 66.0f) * h;
        g.setColour (theme::colours::textSecondary);
        g.fillRect (bounds.getX(), zeroY - 0.75f, bounds.getWidth(), 1.5f);

        if (level > 0.001f)
        {
            const float levelY = bounds.getBottom() - level * h;

            g.saveState();
            g.reduceClipRegion (track);

            // Below 0 dB: ramp gradient. Above 0 dB: error red.
            const float solidTop = juce::jmax (levelY, zeroY);
            juce::ColourGradient grad (theme::colours::gradB, 0.0f, bounds.getBottom(),
                                       theme::colours::gradA, 0.0f, solidTop, false);
            g.setFillType (juce::FillType (grad));
            g.fillRect (bounds.getX(), solidTop, bounds.getWidth(),
                        bounds.getBottom() - solidTop);

            if (levelY < zeroY)
            {
                g.setColour (theme::colours::error);
                g.fillRect (bounds.getX(), levelY, bounds.getWidth(), zeroY - levelY);
            }
            if (clip > 0.0f)
            {
                g.setColour (theme::colours::error);
                g.fillRect (bounds.getX(), bounds.getY(), bounds.getWidth(), 3.0f);
            }
            g.restoreState();
        }

        g.setColour (theme::colours::textSecondary);
        g.setFont (theme::mono (8.5f));
        g.drawText (caption, getLocalBounds().removeFromBottom (13),
                    juce::Justification::centredTop, false);
    }

private:
    juce::String caption;
    float level = 0.0f;
    float clip = 0.0f;
};

} // namespace aki::ui
