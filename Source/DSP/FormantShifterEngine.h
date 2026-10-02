#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace aki::dsp
{

// STFT formant shifter.
//
//   * selectable 128..4096-point FFT (default 2048), Hann window, 4x overlap
//     (hop = size/4), double windowing; the overlap-add reconstructs at
//     sum(w^2)/4 = 1.5 x input
//   * peak-region spectral envelope: one magnitude maximum per width-sized
//     region (accepted only above the region average), linearly interpolated
//     between anchors at bin 0 and bin envelopeBins-1; the region width is
//     defined in bins (reference semantics), so the Bands knob sweeps its
//     whole range at every FFT size
//   * envelope warped by reading at bin k / ratio (ratio = 2^(cents/1200))
//     with linear interpolation; the complex spectrum is scaled by
//     shifted/original, clamped to [0.05, 20], with 1e-9 epsilon floors
//   * overlap-add scaled by 1/6 (JUCE applies the inverse-FFT scaling once)
//
// The latest spectrum frame is published for UI visualisation. Every buffer —
// including one FFT object and one window table per supported order — is
// preallocated in prepare(), so switching the FFT size on the audio thread
// only re-points and clears state, never allocates.
class FormantShifterEngine
{
public:
    static constexpr int minFftOrder = 7;   // 128
    static constexpr int maxFftOrder = 12;  // 4096
    static constexpr int numFftOrders = maxFftOrder - minFftOrder + 1;
    static constexpr int maxFftSize = 1 << maxFftOrder;
    static constexpr int maxBins = maxFftSize / 2 + 1;      // 2049
    static constexpr int maxEnvelopeBins = maxFftSize / 2;  // 2048
    static constexpr int defaultFftOrder = 11;              // 2048
    static constexpr int overlap = 4;

    void prepare (double sampleRate, int channels);
    void setTargets (float shiftCents, float envelopeWidth);

    // Switches the transform size. Realtime-safe: all per-order resources
    // already exist, so this re-points, clears the FIFOs (the wet stream
    // restarts from silence for a moment) and updates the latency.
    void setFftOrder (int order);

    int getFftOrder() const { return activeOrder; }
    int getFftSize() const { return activeFftSize; }
    int getBins() const { return activeBins; }
    int getEnvelopeBins() const { return activeEnvelopeBins; }
    int getLatencySamples() const { return activeFftSize; }

    // Per-sample contract: write every channel's input, then read every
    // channel's output, then advance() exactly once. The frame loop fires
    // inside advance(), after all channels have been written for this
    // sample — matching the FIFO order the algorithm was designed around.
    void writeInput (int channel, float input);
    float readOutput (int channel);
    void advance();

    // Mono convenience: write + read + advance in one call.
    float processSample (int channel, float input);

    void processFrame (int channel);

    // Copies the most recent spectrum frame (bins magnitudes) and the current
    // peak-region envelope (envelopeBins values) for UI use. The frame
    // counter increments once per published frame, letting the UI skip
    // repaints when nothing new arrived.
    void copyLatestSpectrum (float* destination, int count) const;
    void copyLatestEnvelope (float* destination, int count) const;
    uint32_t getFrameCounter() const { return frameCounter.load (std::memory_order_acquire); }
    // The band width and bin count used by the latest published frame (for
    // band-boundary ticks and axis scaling in the UI).
    int getPublishedWidth() const { return publishedWidth.load (std::memory_order_acquire); }
    int getPublishedBins() const { return publishedBins.load (std::memory_order_acquire); }

private:
    static constexpr int maxChannels = 2;

    // juce::dsp::FFT and WindowingFunction are fixed-size and non-copyable,
    // so one of each order is built once and selected by index.
    std::unique_ptr<juce::dsp::FFT> fftByOrder[(size_t) numFftOrders];
    std::unique_ptr<juce::dsp::WindowingFunction<float>> windowsByOrder[(size_t) numFftOrders];

    juce::AudioBuffer<float> inputFifo, outputFifo, fftData;
    std::vector<float> frameMagnitudes, interpolated, peakValues;
    std::vector<float> postMagnitudes, shiftedEnvelope;
    std::vector<int> peakIndices;

    int activeOrder = defaultFftOrder;
    int activeFftSize = 1 << defaultFftOrder;
    int activeBins = activeFftSize / 2 + 1;
    int activeEnvelopeBins = activeFftSize / 2;
    int fifoIndex = 0;
    int outputIndex = 0;
    int activeChannels = 2;
    float currentShiftCents = 0.0f;
    float currentEnvelopeWidth = 10.0f;

    juce::SmoothedValue<float> shiftSmoothed, envelopeSmoothed;

    // Triple-buffered spectrum + envelope snapshots, published from the
    // audio thread and read from the message thread without locks.
    std::array<std::array<float, (size_t) maxBins>, 3> spectrumSlots {};
    std::array<std::array<float, (size_t) maxBins>, 3> envelopeSlots {};
    std::atomic<uint32_t> spectrumLatest { 0u };
    std::atomic<uint32_t> frameCounter { 0u };
    std::atomic<int> publishedWidth { 10 };
    std::atomic<int> publishedBins { (1 << defaultFftOrder) / 2 + 1 };
};

} // namespace aki::dsp
