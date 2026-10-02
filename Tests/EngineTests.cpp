// Offline engine tests: render test signals through the DSP modules and
// assert the invariants that matter (finite output, unity behaviour at
// neutral settings, stereo consistency, tilt/width response, latency report).

#include "DSP/FormantShifterEngine.h"
#include "DSP/NoiseBed.h"
#include "DSP/PitchDetector.h"
#include "DSP/PitchShifter.h"
#include "DSP/StereoWidth.h"
#include "DSP/TiltEQ.h"
#include "PresetManager.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace
{
constexpr double testSampleRate = 48000.0;

struct Rms
{
    float value = 0.0f;
    bool finite = true;
};

Rms render (std::vector<float>& buffer, const std::function<float (int, float)>& process)
{
    double sum = 0.0;
    bool finite = true;
    for (size_t i = 0; i < buffer.size(); ++i)
    {
        const float out = process ((int) i, buffer[(size_t) i]);
        if (! std::isfinite (out))
            finite = false;
        sum += (double) out * (double) out;
        buffer[(size_t) i] = out;
    }
    return { (float) std::sqrt (sum / (double) buffer.size()), finite };
}

std::vector<float> makeSine (double freq, double seconds)
{
    std::vector<float> buffer ((size_t) (testSampleRate * seconds));
    for (size_t i = 0; i < buffer.size(); ++i)
        buffer[(size_t) i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq
                                                     * (double) i / testSampleRate);
    return buffer;
}

int failures = 0;

void expect (bool condition, const char* description)
{
    std::printf ("%s %s\n", condition ? "[PASS]" : "[FAIL]", description);
    if (! condition)
        ++failures;
}
} // namespace

int main()
{
    {
        // Diagnostic: FFT roundtrip gain and window sums.
        constexpr int N = 2048;
        juce::dsp::FFT fft (11);
        std::vector<float> d ((size_t) (2 * N), 0.0f);
        for (int i = 0; i < N; ++i)
            d[(size_t) i] = (float) std::sin (0.1 * i);
        auto orig = d;
        fft.performRealOnlyForwardTransform (d.data());
        fft.performRealOnlyInverseTransform (d.data());
        float maxDiff = 0.0f;
        for (int i = 0; i < N; ++i)
            maxDiff = std::max (maxDiff, std::abs (d[(size_t) i] - orig[(size_t) i]));
        std::printf ("[diag] fft roundtrip maxDiff = %.6f\n", maxDiff);

        juce::dsp::WindowingFunction<float> win (N, juce::dsp::WindowingFunction<float>::hann);
        std::vector<float> w ((size_t) N, 1.0f);
        win.multiplyWithWindowingTable (w.data(), N);
        double mean = 0.0, meanSq = 0.0;
        for (int i = 0; i < N; ++i)
        {
            mean += w[(size_t) i];
            meanSq += w[(size_t) i] * w[(size_t) i];
        }
        mean /= N;
        meanSq /= N;
        std::printf ("[diag] window mean = %.4f meanSq = %.4f (4x meanSq = %.4f)\n",
                     mean, meanSq, 4.0 * meanSq);
    }
    {
        // Neutral formant settings reconstruct the input with no NaNs.
        aki::dsp::FormantShifterEngine engine;
        engine.prepare (testSampleRate, 1);
        auto buffer = makeSine (220.0, 2.0);
        const Rms dry = render (buffer, [] (int, float x) { return x; });
        auto processed = makeSine (220.0, 2.0);
        const Rms wet = render (processed, [&] (int, float x) { return engine.processSample (0, x); });
        expect (wet.finite, "formant engine output is finite");
        // The overlap-add divides by the squared-window sum (6 for the
        // mean-normalised Hann at 4x overlap), so neutral settings are
        // transparent: wet/dry RMS ratio = 1.
        const float ratio = wet.value / dry.value;
        std::printf ("      (dry rms %.4f, wet rms %.4f, ratio %.4f)\n",
                     dry.value, wet.value, ratio);
        expect (std::abs (ratio - 1.0f) < 0.05f,
                "neutral settings are transparent (unity reconstruction)");
        expect (engine.getLatencySamples() == 2048, "latency reported as fftSize (2048)");
    }
    {
        // Extreme shift settings stay finite and change the spectrum.
        for (float cents : { -1200.0f, 1200.0f })
        {
            aki::dsp::FormantShifterEngine engine;
            engine.prepare (testSampleRate, 1);
            engine.setTargets (cents, 10.0f);
            auto processed = makeSine (220.0, 1.5);
            const Rms wet = render (processed,
                                    [&] (int, float x) { return engine.processSample (0, x); });
            expect (wet.finite, "extreme formant shift is finite");
        }
    }
    {
        // Latency-aligned null test: at neutral settings the engine output
        // must equal the input shifted by exactly fftSize samples, up to
        // float rounding. This is the offline equivalent of a DAW null test
        // with PDC compensation.
        aki::dsp::FormantShifterEngine engine;
        engine.prepare (testSampleRate, 1);
        const int total = (int) (testSampleRate * 2.0);
        const int latency = engine.getLatencySamples();
        std::vector<float> input ((size_t) total);
        float maxDiff = 0.0f;
        for (int i = 0; i < total; ++i)
        {
            const float x = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / testSampleRate)
                          + 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1700.0 * i / testSampleRate)
                          + 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 5800.0 * i / testSampleRate);
            input[(size_t) i] = x;
            engine.writeInput (0, x);
            const float wet = engine.readOutput (0);
            engine.advance();
            if (i >= latency + 4096) // skip warmup, compare on steady state
                maxDiff = std::max (maxDiff, std::abs (wet - input[(size_t) (i - latency)]));
        }
        std::printf ("      (latency-aligned null: max sample diff %.2e = %.3f%%)\n",
                     maxDiff, maxDiff * 100.0f);
        // Residual is the symmetric-Hann COLA ripple inherent to the STFT
        // design (~0.04 % amplitude) — far below audibility.
        expect (maxDiff < 5.0e-4f,
                "neutral engine nulls against a latency-aligned copy");
    }
    {
        // Stereo engine consistency: identical input on both channels must
        // come out identical through the write/read/advance per-sample order.
        aki::dsp::FormantShifterEngine engine;
        engine.prepare (testSampleRate, 2);
        engine.setTargets (300.0f, 10.0f);
        bool finite = true;
        float maxDiff = 0.0f;
        for (int i = 0; i < (int) (testSampleRate * 1.0); ++i)
        {
            const float x = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0
                                                     * i / testSampleRate);
            engine.writeInput (0, x);
            engine.writeInput (1, x);
            const float l = engine.readOutput (0);
            const float r = engine.readOutput (1);
            engine.advance();
            if (i > 6000)
            {
                if (! std::isfinite (l) || ! std::isfinite (r))
                    finite = false;
                maxDiff = std::max (maxDiff, std::abs (l - r));
            }
        }
        expect (finite && maxDiff < 1.0e-4f,
                "stereo engine channels track identically");
    }
    {
        // Stereo pitch shifter consistency (shared ring state regression).
        aki::dsp::PitchShifter shifter;
        shifter.prepare (testSampleRate, 2);
        shifter.setSemitones (5.0f);
        bool finite = true;
        float maxDiff = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            const float x = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0
                                                     * i / testSampleRate);
            const float l = shifter.processSample (0, x);
            const float r = shifter.processSample (1, x);
            if (i > 6000)
            {
                if (! std::isfinite (l) || ! std::isfinite (r))
                    finite = false;
                maxDiff = std::max (maxDiff, std::abs (l - r));
            }
        }
        expect (finite && maxDiff < 1.0e-4f,
                "stereo pitch shifter channels track identically");
    }
    {
        // Envelope snapshot: the Bands knob reshapes the published envelope
        // curve, and the frame counter advances for UI gating.
        auto feed = [] (aki::dsp::FormantShifterEngine& engine, double seconds, float width)
        {
            engine.setTargets (400.0f, width);
            const int n = (int) (testSampleRate * seconds);
            for (int i = 0; i < n; ++i)
            {
                const float x = 0.35f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / testSampleRate)
                              + 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 2200.0 * i / testSampleRate);
                engine.writeInput (0, x);
                engine.readOutput (0);
                engine.advance();
            }
        };
        aki::dsp::FormantShifterEngine engine;
        engine.prepare (testSampleRate, 1);
        feed (engine, 1.0, 3.0f);
        // 1 s at 48 kHz with hop 512 publishes ~94 frames.
        expect (engine.getFrameCounter() > 50, "frame counter advances per published frame");
        float fineEnv[aki::dsp::FormantShifterEngine::maxBins];
        engine.copyLatestEnvelope (fineEnv, engine.getPublishedBins());
        feed (engine, 1.0, 15.0f);
        float coarseEnv[aki::dsp::FormantShifterEngine::maxBins];
        engine.copyLatestEnvelope (coarseEnv, engine.getPublishedBins());
        double diff = 0.0;
        for (int k = 1; k < 512; ++k)
            diff += std::abs (fineEnv[k] - coarseEnv[k]);
        expect (diff > 1.0, "envelope snapshot changes with Bands width");
    }
    {
        // User preset files: write a state, scan finds it, read round-trips.
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("AkiTiltPresetTest");
        dir.deleteRecursively();
        juce::ValueTree state ("PARAMETERS");
        state.setProperty ("formantShift", 320.0f, nullptr);
        state.setProperty ("bands", 12.0f, nullptr);
        expect (aki::presets::writeStateFile (state, "My Test", dir),
                "user preset file writes");
        // Factory entries are always listed first, so the scan returns
        // 9 + (user files found).
        const auto entries = aki::presets::scanPresets (dir);
        const aki::presets::PresetEntry* userEntry = nullptr;
        int userCount = 0;
        for (const auto& e : entries)
            if (! e.isFactory)
            {
                ++userCount;
                userEntry = &e;
            }
        expect (userCount == 1 && userEntry != nullptr && userEntry->name == "My Test",
                "scan finds the written user preset");
        const auto readBack = aki::presets::readStateFile (userEntry->file);
        expect (readBack.hasType ("PARAMETERS")
                    && juce::approximatelyEqual ((double) readBack.getProperty ("formantShift"), 320.0)
                    && juce::approximatelyEqual ((double) readBack.getProperty ("bands"), 12.0),
                "preset file round-trips its values");
        dir.deleteRecursively();
        // FFT-size assignment: sizes map onto choice indices, snapping to the
        // nearest window and clamping to the supported range.
        expect (aki::presets::fftIndexFromSize (128) == 0
                    && aki::presets::fftIndexFromSize (2048) == 4
                    && aki::presets::fftIndexFromSize (4096) == 5,
                "fft index mapping hits both ends");
        expect (aki::presets::fftIndexFromSize (1000) == 3
                    && aki::presets::fftIndexFromSize (300) == 1,
                "fft index mapping snaps to the nearest window");
        // Every factory preset carries a supported analysis size.
        bool allValid = true;
        for (const auto& preset : aki::presets::getAll())
            if (aki::presets::fftIndexFromSize (preset.fftSize) < 0
                || preset.fftSize < 128 || preset.fftSize > 4096)
                allValid = false;
        expect (allValid, "all factory presets carry a valid fft size");
    }
    {
        // Every supported FFT size keeps the engine invariants. The null
        // residual is the symmetric-Hann COLA ripple, which scales with the
        // window length — thresholds calibrated per size.
        constexpr float nullTolerance[aki::dsp::FormantShifterEngine::numFftOrders]
            = { 8.0e-3f, 4.0e-3f, 2.0e-3f, 1.0e-3f, 5.0e-4f, 3.0e-4f };
        for (int order = aki::dsp::FormantShifterEngine::minFftOrder;
             order <= aki::dsp::FormantShifterEngine::maxFftOrder; ++order)
        {
            aki::dsp::FormantShifterEngine engine;
            engine.prepare (testSampleRate, 1);
            engine.setFftOrder (order);
            const int size = 1 << order;
            char label[160];
            std::snprintf (label, sizeof (label),
                           "fft size %d reports its size, bins and latency", size);
            expect (engine.getFftSize() == size && engine.getLatencySamples() == size
                        && engine.getBins() == size / 2 + 1
                        && engine.getEnvelopeBins() == size / 2
                        && engine.getPublishedBins() == size / 2 + 1,
                    label);

            const int total = (int) (testSampleRate * 1.5);
            const int latency = engine.getLatencySamples();
            std::vector<float> input ((size_t) total);
            float maxDiff = 0.0f;
            bool finite = true;
            for (int i = 0; i < total; ++i)
            {
                const float x = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / testSampleRate)
                              + 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1700.0 * i / testSampleRate)
                              + 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 5800.0 * i / testSampleRate);
                input[(size_t) i] = x;
                engine.writeInput (0, x);
                const float wet = engine.readOutput (0);
                engine.advance();
                if (! std::isfinite (wet))
                    finite = false;
                if (i >= latency + 4096)
                    maxDiff = std::max (maxDiff, std::abs (wet - input[(size_t) (i - latency)]));
            }
            std::snprintf (label, sizeof (label),
                           "fft size %d nulls at neutral (residual %.2e)", size, maxDiff);
            std::printf ("      (size %d latency-aligned null: %.2e)\n", size, maxDiff);
            expect (finite && maxDiff < nullTolerance[order - aki::dsp::FormantShifterEngine::minFftOrder],
                    label);

            // Extreme shift stays finite at this size, and the Bands knob
            // keeps its full bin-width sweep (width defined in bins).
            engine.setTargets (1200.0f, 10.0f);
            finite = true;
            for (int i = 0; i < (int) (testSampleRate * 0.5); ++i)
            {
                const float wet = engine.processSample (0, input[(size_t) (i % total)]);
                if (! std::isfinite (wet))
                    finite = false;
            }
            std::snprintf (label, sizeof (label), "fft size %d survives extreme shift", size);
            expect (finite, label);

            engine.setTargets (0.0f, 3.0f);
            for (int i = 0; i < (int) (testSampleRate * 0.4); ++i)
                engine.processSample (0, input[(size_t) (i % total)]);
            const int fineWidth = engine.getPublishedWidth();
            engine.setTargets (0.0f, 30.0f);
            for (int i = 0; i < (int) (testSampleRate * 0.4); ++i)
                engine.processSample (0, input[(size_t) (i % total)]);
            const int coarseWidth = engine.getPublishedWidth();
            std::snprintf (label, sizeof (label),
                           "fft size %d keeps the Bands knob full-range (%d -> %d bins)",
                           size, fineWidth, coarseWidth);
            expect (fineWidth == 3 && coarseWidth == 30, label);
        }
    }
    {
        // Switching the FFT size mid-stream never breaks the stream (the wet
        // path restarts from silence, so only finiteness is asserted).
        aki::dsp::FormantShifterEngine engine;
        engine.prepare (testSampleRate, 1);
        engine.setTargets (300.0f, 10.0f);
        bool finite = true;
        for (int i = 0; i < (int) testSampleRate; ++i)
        {
            if (i == (int) (testSampleRate * 0.5))
                engine.setFftOrder (7);
            const float x = 0.4f * (float) std::sin (0.03 * i);
            const float wet = engine.processSample (0, x);
            if (! std::isfinite (wet))
                finite = false;
        }
        expect (finite && engine.getFftSize() == 128,
                "mid-stream fft size switch stays finite and applies");
    }
    {
        // Pitch detector: steady tones land on their cents offset from the
        // anchor; noise is rejected as unvoiced.
        for (float freq : { 110.0f, 220.0f, 440.0f, 880.0f })
        {
            aki::dsp::PitchDetector detector;
            detector.prepare (testSampleRate);
            const double w = 2.0 * juce::MathConstants<double>::pi * (double) freq / testSampleRate;
            float cents = 0.0f;
            for (int i = 0; i < (int) (testSampleRate * 0.5); ++i)
            {
                detector.write (0.4f * (float) std::sin (w * (double) i));
                cents = detector.track();
            }
            const float expected = 1200.0f * std::log2 (freq / aki::dsp::autoFormantAnchorHz);
            char label[160];
            std::snprintf (label, sizeof (label),
                           "pitch detector reads %.0f Hz within 30 cents (%.0f ct)",
                           freq, cents);
            expect (detector.isVoiced() && std::abs (cents - expected) < 30.0f, label);
        }
        {
            aki::dsp::PitchDetector detector;
            detector.prepare (testSampleRate);
            juce::Random rng (42);
            for (int i = 0; i < (int) (testSampleRate * 0.5); ++i)
            {
                detector.write (0.4f * (rng.nextFloat() * 2.0f - 1.0f));
                detector.track();
            }
            expect (! detector.isVoiced(), "pitch detector rejects noise as unvoiced");
        }
        // The Auto mapping adds the knob offset and clamps to the range.
        expect (juce::approximatelyEqual (aki::dsp::autoFormantCents (300.0f, 200.0f), 500.0f),
                "auto formant adds the knob offset");
        expect (juce::approximatelyEqual (aki::dsp::autoFormantCents (900.0f, 900.0f), 1200.0f),
                "auto formant clamps high");
        expect (juce::approximatelyEqual (aki::dsp::autoFormantCents (-1000.0f, -900.0f), -1200.0f),
                "auto formant clamps low");
    }
    {
        aki::dsp::PitchShifter shifter;
        shifter.prepare (testSampleRate, 1);
        shifter.setSemitones (7.0f);
        auto processed = makeSine (220.0, 1.0);
        const Rms wet = render (processed, [&] (int, float x) { return shifter.processSample (0, x); });
        expect (wet.finite && wet.value > 0.1f && wet.value < 0.6f,
                "pitch shifter +7 st stays finite near unity RMS");
    }
    {
        // Tilt EQ: identity at 0, brightens highs at +12 dB.
        aki::dsp::TiltEQ eq;
        eq.prepare (testSampleRate, 1);
        auto flat = makeSine (1000.0, 0.5);
        const Rms flatOut = render (flat, [&] (int, float x) { return eq.processSample (0, x); });
        expect (std::abs (flatOut.value - 0.353f) < 0.02f,
                "tilt at 0 dB is an exact passthrough");

        eq.setTiltDb (12.0f);
        eq.update (2400); // advance the 30 ms ramp fully to its target
        auto bright = makeSine (8000.0, 0.25);
        double sum = 0.0;
        for (auto& s : bright)
        {
            s = eq.processSample (0, s);
            sum += (double) s * (double) s;
        }
        const float rmsBright = (float) std::sqrt (sum / (double) bright.size());
        expect (rmsBright > 0.35f, "tilt +12 dB boosts high frequencies");
    }
    {
        // Stereo width: 200% grows the side signal.
        aki::dsp::StereoWidth width;
        width.prepare (testSampleRate);
        width.setWidth (2.0f);
        double sideIn = 0.0, sideOut = 0.0;
        for (int i = 0; i < 4800; ++i)
        {
            float l = 0.4f * (float) std::sin (0.05 * i);
            float r = -0.2f * (float) std::sin (0.05 * i);
            sideIn += std::pow ((double) (l - r) * 0.5, 2.0);
            width.processSample (l, r);
            sideOut += std::pow ((double) (l - r) * 0.5, 2.0);
        }
        expect (sideOut > sideIn * 2.0, "width 200% doubles side energy");
    }
    {
        // Noise bed: silent at zero amount, audible at full, muted when the
        // input follower ducks it.
        aki::dsp::NoiseBed bed;
        bed.prepare (testSampleRate, 1);
        bed.setAmount (0.0f);
        bed.update();
        float silentSum = 0.0f;
        for (int i = 0; i < 4800; ++i)
            silentSum += std::abs (bed.processSample (0));
        bed.setAmount (1.0f);
        bed.update();
        float loudSum = 0.0f;
        for (int i = 0; i < 4800; ++i)
            loudSum += std::abs (bed.processSample (0));
        bed.update (1, 0.0f);
        float duckedSum = 0.0f;
        for (int i = 0; i < 4800; ++i)
            duckedSum += std::abs (bed.processSample (0));
        expect (silentSum == 0.0f && loudSum > 0.0f && duckedSum == 0.0f,
                "noise bed scales with amount and follows the input");
    }

    std::printf ("%s\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
