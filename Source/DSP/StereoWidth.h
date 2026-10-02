#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace aki::dsp
{

// Mid/side stereo width control on the wet path. Unity at 100%.
class StereoWidth
{
public:
    void prepare (double sampleRate)
    {
        width.reset (sampleRate, 0.03);
        width.setCurrentAndTargetValue (1.0f);
    }

    void setWidth (float w) { width.setTargetValue (juce::jlimit (0.0f, 2.0f, w)); }

    void processSample (float& left, float& right)
    {
        const float w = width.getNextValue();
        if (std::abs (w - 1.0f) < 0.0005f)
            return;
        const float mid = 0.5f * (left + right);
        const float side = 0.5f * (left - right);
        left = mid + side * w;
        right = mid - side * w;
    }

private:
    juce::SmoothedValue<float> width;
};

} // namespace aki::dsp
