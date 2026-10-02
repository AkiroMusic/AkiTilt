#pragma once

#include <vector>

namespace aki::dsp
{

// Per-channel ring buffer used to keep the dry path aligned with the STFT
// wet path so the crossfade cannot comb-filter. The ring is allocated at the
// maximum delay once; the actual delay length can be changed on the audio
// thread (when the FFT size changes) without reallocating.
class DelayLine
{
public:
    void prepare (int maxDelaySamples, int channels)
    {
        capacity = juce::jmax (1, maxDelaySamples);
        delay = capacity;
        writeIndex = 0;
        data.assign ((size_t) capacity * (size_t) juce::jmax (1, channels), 0.0f);
    }

    // Realtime-safe delay-length change within the preallocated capacity.
    void setDelay (int delaySamples)
    {
        delay = juce::jlimit (1, capacity, delaySamples);
    }

    int getDelay() const { return delay; }

    // Call once per sample for every channel, then advance().
    float processSample (int channel, float input)
    {
        auto* d = data.data() + (size_t) capacity * (size_t) channel;
        int readIndex = writeIndex + capacity - delay;
        if (readIndex >= capacity)
            readIndex -= capacity;
        const float delayed = d[(size_t) readIndex];
        d[(size_t) writeIndex] = input;
        return delayed;
    }

    void advance()
    {
        if (++writeIndex >= capacity)
            writeIndex = 0;
    }

private:
    int capacity = 1;
    int delay = 1;
    int writeIndex = 0;
    std::vector<float> data;
};

} // namespace aki::dsp
