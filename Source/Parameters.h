#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace aki::params
{

inline constexpr const char* formantShift = "formantShift";
inline constexpr const char* bands        = "bands";
inline constexpr const char* pitchShift   = "pitchShift";
inline constexpr const char* tilt         = "tilt";
inline constexpr const char* dryWet       = "dryWet";
inline constexpr const char* width        = "width";
inline constexpr const char* airAmount    = "airAmount";
inline constexpr const char* airTone      = "airTone";
inline constexpr const char* outputGain   = "outputGain";
inline constexpr const char* limiter      = "limiter";
inline constexpr const char* bypass       = "bypass";
inline constexpr const char* fftSize      = "fftSize";
inline constexpr const char* formantMode  = "formantMode";

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using APF = juce::AudioParameterFloat;
    using APFAttributes = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<APF> (juce::ParameterID { formantShift, 1 }, "Formant",
        juce::NormalisableRange<float> (-1200.0f, 1200.0f, 1.0f), 0.0f,
        APFAttributes().withLabel ("ct")));
    layout.add (std::make_unique<APF> (juce::ParameterID { bands, 1 }, "Bands",
        juce::NormalisableRange<float> (0.0f, 32.0f, 0.01f), 10.0f));
    layout.add (std::make_unique<APF> (juce::ParameterID { pitchShift, 1 }, "Pitch",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        APFAttributes().withLabel ("st")));
    layout.add (std::make_unique<APF> (juce::ParameterID { tilt, 1 }, "Tilt",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        APFAttributes().withLabel ("dB")));
    layout.add (std::make_unique<APF> (juce::ParameterID { dryWet, 1 }, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        APFAttributes().withLabel ("%")));
    layout.add (std::make_unique<APF> (juce::ParameterID { width, 1 }, "Width",
        juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
        APFAttributes().withLabel ("%")));
    layout.add (std::make_unique<APF> (juce::ParameterID { airAmount, 1 }, "Air",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        APFAttributes().withLabel ("%")));
    layout.add (std::make_unique<APF> (juce::ParameterID { airTone, 1 }, "Tone",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        APFAttributes().withLabel ("%")));
    layout.add (std::make_unique<APF> (juce::ParameterID { outputGain, 1 }, "Out",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        APFAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { limiter, 1 }, "Limiter", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { formantMode, 1 }, "Auto Formant", false));

    // Setup switch rather than a performance control: hosts cannot automate
    // it, the user picks it from the panel.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { fftSize, 1 }, "FFT Size",
        juce::StringArray { "128", "256", "512", "1024", "2048", "4096" }, 4,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    return layout;
}

} // namespace aki::params
