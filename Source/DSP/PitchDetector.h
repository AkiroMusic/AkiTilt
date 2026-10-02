#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace aki::dsp
{

// Pure mapping for the Auto formant mode: the detected pitch offset from the
// anchor note plus the manual knob offset (which becomes a ±1200 ct trim in
// Auto), clamped to the formant parameter range. Higher input pitch = higher
// formants, the way a smaller vocal tract sounds.
inline constexpr float autoFormantAnchorHz = 220.0f; // A3

inline float autoFormantCents (float detectedCents, float knobCents)
{
    return juce::jlimit (-1200.0f, 1200.0f, detectedCents + knobCents);
}

// Monophonic fundamental-frequency detector for the Auto formant mode.
//
// The mono input is decimated to a fixed ~12 kHz analysis rate (box filter),
// then a YIN-style cumulative-mean-normalised difference function picks the
// period between 60 and 1000 Hz. Analyses run every ~10.7 ms; a dip of the
// normalised difference below 0.15 marks a voiced frame, otherwise the
// detector is unvoiced and its output glides back to the anchor.
//
// Output: cents relative to the anchor (220 Hz = 0 ct), smoothed — 80 ms
// while voiced, ~1.5 s release back to 0 ct when unvoiced.
class PitchDetector
{
public:
    static constexpr float minHz = 60.0f;
    static constexpr float maxHz = 1000.0f;
    static constexpr float voicedThreshold = 0.15f;

    void prepare (double sampleRate)
    {
        inputRate = sampleRate;
        decimation = juce::jmax (1, juce::roundToInt (sampleRate / 12000.0));
        analysisRate = (float) (sampleRate / (double) decimation);
        minLag = juce::jmax (2, (int) std::floor (analysisRate / maxHz));
        maxLag = (int) std::ceil (analysisRate / minHz);
        windowSamples = 2 * maxLag + 64; // two full periods of the lowest pitch
        hopDecimated = juce::jlimit (32, 512, (int) std::round (analysisRate * 0.0107f));
        buffer.assign ((size_t) (windowSamples + maxLag), 0.0f);
        difference.assign ((size_t) maxLag + 1, 0.0f);
        cmnd.assign ((size_t) maxLag + 1, 1.0f);
        fill = 0;
        decimCount = 0;
        decimAcc = 0.0f;
        voiced = false;
        cents.reset (sampleRate, unvoicedTimeSeconds);
        cents.setCurrentAndTargetValue (0.0f);
    }

    // Feed the mono sum once per sample. Runs the analysis whenever the hop
    // boundary is reached; otherwise just accumulates.
    void write (float inputSample)
    {
        decimAcc += inputSample;
        if (++decimCount < decimation)
            return;
        decimAcc /= (float) decimation;
        decimCount = 0;
        buffer[(size_t) fill++] = decimAcc;
        if (fill < windowSamples + maxLag)
            return;
        analyse();
        const int keep = fill - hopDecimated;
        std::memmove (buffer.data(), buffer.data() + hopDecimated,
                      (size_t) keep * sizeof (float));
        fill = keep;
    }

    // Advances the output smoother exactly one step; call once per sample.
    float track() { return cents.getNextValue(); }

    bool isVoiced() const { return voiced; }

private:
    void analyse()
    {
        // Difference function d(tau) = sum (x[i] - x[i+tau])^2.
        for (int tau = 0; tau <= maxLag; ++tau)
        {
            float sum = 0.0f;
            for (int i = 0; i < windowSamples; ++i)
            {
                const float diff = buffer[(size_t) i] - buffer[(size_t) (i + tau)];
                sum += diff * diff;
            }
            difference[(size_t) tau] = sum;
        }

        // Cumulative-mean-normalised difference (YIN).
        float runningSum = 0.0f;
        cmnd[(size_t) 0] = 1.0f;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            runningSum += difference[(size_t) tau];
            cmnd[(size_t) tau] = runningSum > 0.0f
                ? difference[(size_t) tau] * (float) tau / runningSum
                : 1.0f;
        }

        // First dip below the threshold, then descend to the local minimum.
        int tauEstimate = -1;
        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            if (cmnd[(size_t) tau] < voicedThreshold)
            {
                while (tau + 1 <= maxLag && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                    ++tau;
                tauEstimate = tau;
                break;
            }
        }

        if (tauEstimate > 0)
        {
            // Parabolic interpolation sharpens the period estimate.
            float shift = 0.0f;
            if (tauEstimate + 1 <= maxLag)
            {
                const float y0 = cmnd[(size_t) (tauEstimate - 1)];
                const float y1 = cmnd[(size_t) tauEstimate];
                const float y2 = cmnd[(size_t) (tauEstimate + 1)];
                const float denom = 2.0f * (y0 - 2.0f * y1 + y2);
                if (denom != 0.0f)
                    shift = juce::jlimit (-1.0f, 1.0f, (y0 - y2) / denom);
            }
            const float freq = analysisRate / ((float) tauEstimate + shift);
            // Raw cents from the anchor — clamping to the formant range is
            // the mapping's job (autoFormantCents), not the detector's.
            const float detectedCents = 1200.0f * std::log2 (freq / autoFormantAnchorHz);
            if (! voiced)
            {
                voiced = true;
                cents.reset (inputRate, voicedTimeSeconds);
            }
            cents.setTargetValue (detectedCents);
        }
        else if (voiced)
        {
            voiced = false;
            cents.reset (inputRate, unvoicedTimeSeconds);
            cents.setTargetValue (0.0f);
        }
    }

    static constexpr double voicedTimeSeconds = 0.08;
    static constexpr double unvoicedTimeSeconds = 1.5;

    double inputRate = 48000.0;
    int decimation = 4;
    float analysisRate = 12000.0f;
    int minLag = 12, maxLag = 200;
    int windowSamples = 464;
    int hopDecimated = 128;
    int fill = 0;
    int decimCount = 0;
    float decimAcc = 0.0f;
    bool voiced = false;

    std::vector<float> buffer, difference, cmnd;
    juce::SmoothedValue<float> cents;
};

} // namespace aki::dsp
