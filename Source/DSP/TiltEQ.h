#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace aki::dsp
{

// Single-knob tilt EQ: mirrored shelves around a fixed split frequency so
// positive values brighten while low content recedes (and vice versa), with a
// small loudness compensation. Bit-exact passthrough near 0 dB.
class TiltEQ
{
public:
    static constexpr float splitHz = 650.0f;

    void prepare (double sampleRate, int channels)
    {
        sampleRateHz = sampleRate;
        tilt.reset (sampleRate, 0.03);
        tilt.setCurrentAndTargetValue (0.0f);
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            low[ch].reset();
            high[ch].reset();
        }
        update (juce::jlimit (1, maxChannels, channels));
    }

    void setTiltDb (float db) { tilt.setTargetValue (juce::jlimit (-12.0f, 12.0f, db)); }

    // Call once per audio block, before processing samples.
    void update (int numSamples = 1)
    {
        tilt.skip (juce::jmax (0, numSamples - 1));
        const float t = tilt.getNextValue();
        const bool identity = std::abs (t) < 0.05f;
        makeupGain = identity ? 1.0f
                              : juce::Decibels::decibelsToGain (t * -0.15f);
        bypassed = identity;
        if (identity)
            return;

        auto lowCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sampleRateHz, splitHz, 0.707f, juce::Decibels::decibelsToGain (-t));
        auto highCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRateHz, splitHz, 0.707f, juce::Decibels::decibelsToGain (t));
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            low[ch].coefficients = lowCoeffs;
            high[ch].coefficients = highCoeffs;
        }
    }

    float processSample (int channel, float input)
    {
        if (bypassed)
            return input;
        return high[channel].processSample (low[channel].processSample (input)) * makeupGain;
    }

private:
    static constexpr int maxChannels = 2;

    double sampleRateHz = 48000.0;
    juce::SmoothedValue<float> tilt;
    juce::dsp::IIR::Filter<float> low[maxChannels];
    juce::dsp::IIR::Filter<float> high[maxChannels];
    float makeupGain = 1.0f;
    bool bypassed = true;
};

} // namespace aki::dsp
