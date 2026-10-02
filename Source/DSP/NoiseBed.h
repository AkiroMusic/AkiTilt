#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace aki::dsp
{

// Filtered noise bed mixed into the wet path to suggest a field-recording
// floor (crickets/air). Per-channel decorrelated white noise through a
// bandpass whose centre follows the Tone control (warm hiss .. open air).
class NoiseBed
{
public:
    void prepare (double sampleRate, int channels)
    {
        sampleRateHz = sampleRate;
        channels = juce::jlimit (1, maxChannels, channels);
        activeChannels = channels;
        amount.reset (sampleRate, 0.05);
        amount.setCurrentAndTargetValue (0.0f);
        tone.reset (sampleRate, 0.05);
        tone.setCurrentAndTargetValue (0.5f);
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            bandpass[ch].reset();
            state[ch] = 0x9E3779B9u ^ (juce::uint32) (ch * 0x85EBCA6Bu + 1u);
        }
        update();
    }

    void setAmount (float a) { amount.setTargetValue (juce::jlimit (0.0f, 1.0f, a)); }
    void setTone (float t) { tone.setTargetValue (juce::jlimit (0.0f, 1.0f, t)); }

    // Call once per audio block, before processing samples. Advances both
    // smoothers across the block, tracks the input envelope (the bed level
    // follows the input continuously) and recomputes the level gain.
    void update (int numSamples = 1, float followerLinear = 1.0f)
    {
        amount.skip (juce::jmax (0, numSamples - 1));
        const float a = amount.getNextValue();

        // Linear envelope tracking (no gate/threshold): the bed level tracks
        // the input envelope continuously, so the noise reads as a layer
        // "wrapped around" the sound rather than an ambience that switches
        // on and off. Reaches full level by roughly -12 dBFS input.
        const float followerDb = juce::Decibels::gainToDecibels (juce::jmax (followerLinear, 0.0f), -90.0f);
        const float track = juce::jlimit (0.0f, 1.0f, (followerDb + 60.0f) / 48.0f);
        const float trackCurve = track * track * (3.0f - 2.0f * track);

        // 800 Hz .. 12 kHz sweep. Bandpass power grows with centre frequency
        // (white noise, fixed Q), so normalise by sqrt(2400/freq) to keep the
        // bed's loudness constant while Tone changes its colour.
        tone.skip (juce::jmax (0, numSamples - 1));
        const float t = tone.getNextValue();
        const float freq = 800.0f * std::pow (15.0f, t);
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRateHz, freq, 1.0f);
        for (int ch = 0; ch < activeChannels; ++ch)
            bandpass[ch].coefficients = coeffs;
        const float levelNorm = std::sqrt (2400.0f / freq);

        currentGain = a * a * 0.12f * trackCurve * levelNorm;
    }

    float processSample (int channel)
    {
        auto& s = state[(size_t) channel];
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        const float white = (float) (int32_t) s / 2147483648.0f;
        return bandpass[(size_t) channel].processSample (white) * currentGain;
    }

private:
    static constexpr int maxChannels = 2;

    double sampleRateHz = 48000.0;
    int activeChannels = 2;
    juce::SmoothedValue<float> amount;
    juce::SmoothedValue<float> tone;
    juce::dsp::IIR::Filter<float> bandpass[maxChannels];
    juce::uint32 state[maxChannels] = { 1u, 2u };
    float currentGain = 0.0f;
};

} // namespace aki::dsp
