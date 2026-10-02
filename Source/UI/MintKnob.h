#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace aki::ui
{

// Rotary control composite: eyebrow title above, L&F-drawn rotary, mono
// numeric readout below. Owns its SliderAttachment.
class MintKnob final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    enum class Format { Cents, Bands, Semitones, Decibels, Percent };

    MintKnob (juce::AudioProcessorValueTreeState& apvts, const char* paramId,
              const juce::String& titleText, Format valueFormat)
        : valueFormatEnum (valueFormat),
          attachment (apvts, paramId, slider)
    {
        title.setText (titleText.toUpperCase(), juce::dontSendNotification);
        title.setJustificationType (juce::Justification::centred);
        title.setFont (theme::sansSemiBold (10.5f));
        title.setColour (juce::Label::textColourId, theme::colours::textSecondary);
        addAndMakeVisible (title);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, theme::colours::accent);
        addAndMakeVisible (slider);

        readout.setJustificationType (juce::Justification::centred);
        readout.setFont (theme::mono (13.0f));
        readout.setColour (juce::Label::textColourId, theme::colours::textPrimary);
        readout.setText (format (slider.getValue()), juce::dontSendNotification);
        addAndMakeVisible (readout);

        slider.onValueChange = [this]()
        { readout.setText (format (slider.getValue()), juce::dontSendNotification); };

    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        title.setBounds (bounds.removeFromTop (14));
        readout.setBounds (bounds.removeFromBottom (16));
        slider.setBounds (bounds.withSizeKeepingCentre (bounds.getWidth(),
                                                        juce::jmin (bounds.getHeight(), bounds.getWidth())));
    }



private:
    juce::String format (double value) const
    {
        const juce::String sign = value > 0.0 ? "+" : "";
        switch (valueFormatEnum)
        {
            case Format::Cents:     return sign + juce::String (value, 0) + " ct";
            case Format::Bands:     return juce::String (value, 1);
            case Format::Semitones: return sign + juce::String (value, 2) + " st";
            case Format::Decibels:  return sign + juce::String (value, 1) + " dB";
            case Format::Percent:   return juce::String (value, 0) + " %";
        }
        return juce::String (value);
    }

    juce::Label title;
    juce::Slider slider;
    juce::Label readout;
    Format valueFormatEnum;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

} // namespace aki::ui
