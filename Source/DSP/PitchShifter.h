#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <vector>

namespace aki::dsp
{

// Dual-window delay-line pitch shifter: two Hann-shaped grains read the
// delay ring at a sliding offset, crossfading power-complementarily so their
// envelopes sum to unity. Sits after the formant STFT; hard-bypasses to an
// exact passthrough near 0 semitones.
class PitchShifter
{
public:
    static constexpr int windowSize = 2048;
    static constexpr int bufferSize = 4096;
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate, int channels)
    {
        channels = juce::jlimit (1, maxChannels, channels);
        semitones.reset (sampleRate, 0.03);
        semitones.setCurrentAndTargetValue (0.0f);
        buffer.assign ((size_t) bufferSize * (size_t) channels, 0.0f);
        phase.assign ((size_t) channels, 0.0f);
        writePositions.assign ((size_t) channels, 0);
        // Precomputed grain envelope (one cos call per entry instead of one
        // per sample per grain), with an extra wrap entry for interpolation.
        grainTable.resize (windowSize + 1);
        for (int i = 0; i < windowSize; ++i)
        {
            const float t = (float) i / (float) windowSize;
            grainTable[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * t));
        }
        grainTable[(size_t) windowSize] = grainTable[(size_t) 0];
    }

    void setSemitones (float st) { semitones.setTargetValue (juce::jlimit (-12.0f, 12.0f, st)); }

    float processSample (int channel, float input)
    {
        auto* ring = buffer.data() + (size_t) bufferSize * (size_t) channel;
        int& writeIndex = writePositions[(size_t) channel];
        ring[writeIndex] = input;

        // Only the first channel advances the smoother so the rate stays
        // one step per sample regardless of channel count.
        const float st = channel == 0 ? semitones.getNextValue()
                                      : semitones.getCurrentValue();
        const float ratio = std::exp2 (st * (1.0f / 12.0f));
        float out = 0.0f;

        if (std::abs (st) > 0.005f)
        {
            float p = phase[(size_t) channel];
            for (int grain = 0; grain < 2; ++grain)
            {
                float pg = std::fmod (p + (float) grain * (windowSize / 2.0f), (float) windowSize);
                if (pg < 0.0f)
                    pg += (float) windowSize;
                const int offset = (int) ((float) windowSize - pg);
                const int readPos = (writeIndex - offset + bufferSize) % bufferSize;
                const int idx = (int) pg;
                const float frac = pg - (float) idx;
                const float envelope = grainTable[(size_t) idx] * (1.0f - frac)
                                     + grainTable[(size_t) idx + 1] * frac;
                out += ring[readPos] * envelope;
            }
        }
        else
        {
            out = input;
        }

        float& p = phase[(size_t) channel];
        p += (ratio - 1.0f);
        p = std::fmod (p + (float) windowSize, (float) windowSize);

        writeIndex = (writeIndex + 1) % bufferSize;
        return out;
    }

private:
    juce::SmoothedValue<float> semitones;
    std::vector<float> buffer;
    std::vector<float> phase;
    std::vector<int> writePositions;
    std::vector<float> grainTable;
};

} // namespace aki::dsp
