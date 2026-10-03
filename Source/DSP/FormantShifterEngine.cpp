#include "FormantShifterEngine.h"

namespace aki::dsp
{

void FormantShifterEngine::prepare (double sampleRate, int channels)
{
    // Always serve both channels. Hosts may call prepareToPlay before the
    // bus layout settles (reporting mono), and a mono preparation would leave
    // channel 1 silent once stereo blocks arrive. Mono buses still loop over
    // their actual channel count in processBlock, so nothing else changes.
    juce::ignoreUnused (channels);
    activeChannels = maxChannels;

    // One transform object and one window table per supported order. Both are
    // fixed-size in JUCE and non-copyable, so the whole set is built once and
    // merely selected by index afterwards.
    if (fftByOrder[0] == nullptr)
    {
        for (int o = 0; o < numFftOrders; ++o)
        {
            const int order = minFftOrder + o;
            fftByOrder[(size_t) o] = std::make_unique<juce::dsp::FFT> (order);
            windowsByOrder[(size_t) o] = std::make_unique<juce::dsp::WindowingFunction<float>> (
                (size_t) 1 << order, juce::dsp::WindowingFunction<float>::hann);
        }
    }

    inputFifo.setSize (maxChannels, maxFftSize, false, true, true);
    outputFifo.setSize (maxChannels, maxFftSize, true, true, true);
    fftData.setSize (maxChannels, maxFftSize * 2, true, true, true);
    frameMagnitudes.assign ((size_t) maxBins, 0.0f);
    interpolated.assign ((size_t) maxEnvelopeBins, 0.0f);
    postMagnitudes.assign ((size_t) maxBins, 0.0f);
    shiftedEnvelope.assign ((size_t) maxEnvelopeBins, 0.0f);
    peakValues.assign ((size_t) maxEnvelopeBins / 2 + 2, 0.0f);
    peakIndices.assign ((size_t) maxEnvelopeBins / 2 + 2, 0);
    fifoIndex = outputIndex = 0;
    shiftSmoothed.reset (sampleRate, 0.02);
    envelopeSmoothed.reset (sampleRate, 0.02);
    shiftSmoothed.setCurrentAndTargetValue (0.0f);
    envelopeSmoothed.setCurrentAndTargetValue (10.0f);
    spectrumLatest.store (0u, std::memory_order_relaxed);
    publishedBins.store (activeBins, std::memory_order_release);
}

void FormantShifterEngine::setFftOrder (int order)
{
    order = juce::jlimit (minFftOrder, maxFftOrder, order);
    if (order == activeOrder)
        return;
    activeOrder = order;
    activeFftSize = 1 << order;
    activeBins = activeFftSize / 2 + 1;
    activeEnvelopeBins = activeFftSize / 2;
    fifoIndex = 0;
    outputIndex = 0;
    inputFifo.clear();
    outputFifo.clear();
    fftData.clear();
    spectrumLatest.store (0u, std::memory_order_relaxed);
    publishedBins.store (activeBins, std::memory_order_release);
}

void FormantShifterEngine::setTargets (float shiftCents, float envelopeWidth)
{
    shiftSmoothed.setTargetValue (shiftCents);
    envelopeSmoothed.setTargetValue (envelopeWidth);
}

float FormantShifterEngine::processSample (int channel, float input)
{
    writeInput (channel, input);
    const float wet = readOutput (channel);
    advance();
    return wet;
}

void FormantShifterEngine::writeInput (int channel, float input)
{
    inputFifo.setSample (channel, fifoIndex, input);
}

float FormantShifterEngine::readOutput (int channel)
{
    const float wet = outputFifo.getSample (channel, outputIndex);
    outputFifo.setSample (channel, outputIndex, 0.0f);
    return wet;
}

void FormantShifterEngine::advance()
{
    currentShiftCents = shiftSmoothed.getNextValue();
    currentEnvelopeWidth = envelopeSmoothed.getNextValue();

    const int hop = activeFftSize / overlap;
    ++fifoIndex;
    outputIndex = (outputIndex + 1) % activeFftSize;
    if (fifoIndex == activeFftSize)
    {
        for (int ch = 0; ch < activeChannels; ++ch)
            processFrame (ch);
        for (int ch = 0; ch < activeChannels; ++ch)
            std::memmove (inputFifo.getWritePointer (ch),
                          inputFifo.getReadPointer (ch) + hop,
                          (size_t) (activeFftSize - hop) * sizeof (float));
        fifoIndex = activeFftSize - hop;
    }
}

void FormantShifterEngine::processFrame (int channel)
{
    auto* data = fftData.getWritePointer (channel);
    auto* input = inputFifo.getReadPointer (channel);
    auto* output = outputFifo.getWritePointer (channel);
    const int fftSize = activeFftSize;
    const int numBins = activeBins;
    const int envelopeBins = activeEnvelopeBins;
    auto& fft = *fftByOrder[(size_t) (activeOrder - minFftOrder)];
    auto& window = *windowsByOrder[(size_t) (activeOrder - minFftOrder)];

    std::copy (input, input + fftSize, data);

    std::fill (data + fftSize, data + fftSize * 2, 0.0f);
    window.multiplyWithWindowingTable (data, (size_t) fftSize);
    fft.performRealOnlyForwardTransform (data);

    frameMagnitudes[(size_t) 0] = std::abs (data[0]) + 1.0e-9f;
    frameMagnitudes[(size_t) (numBins - 1)] = std::abs (data[1]) + 1.0e-9f;
    for (int k = 1; k < numBins - 1; ++k)
    {
        const float re = data[2 * k], im = data[2 * k + 1];
        const float magnitude = std::sqrt (re * re + im * im) + 1.0e-9f;
        frameMagnitudes[(size_t) k] = magnitude;
    }

    // Peak-region envelope: one maximum per width-sized region (accepted only
    // above the region average), interpolated into an envelope, shifted, then
    // reapplied as per-bin gains. The width is defined in BINS (reference
    // semantics) so the Bands knob keeps its full sweep at every FFT size —
    // at smaller sizes the same width simply covers proportionally more Hz.
    const int width = juce::jlimit (2, juce::jmax (2, envelopeBins / 2),
                                    juce::roundToInt (currentEnvelopeWidth));
    const int regionCount = (envelopeBins + width - 1) / width;

    peakValues[(size_t) 0] = frameMagnitudes[(size_t) 0];
    peakIndices[(size_t) 0] = 0;
    peakValues[(size_t) regionCount + 1] = frameMagnitudes[(size_t) (envelopeBins - 1)];
    peakIndices[(size_t) regionCount + 1] = envelopeBins - 1;

    for (int region = 0; region < regionCount; ++region)
    {
        const int first = region * width;
        const int last = juce::jmin (first + width, envelopeBins);
        float sum = 0.0f;
        for (int i = first; i < last; ++i)
            sum += frameMagnitudes[(size_t) i];
        const float average = sum / (float) juce::jmax (1, last - first);
        float localMaximum = 0.0f;
        int maximumIndex = first;
        for (int i = first; i < last; ++i)
        {
            const float current = frameMagnitudes[(size_t) i];
            if (current > average && current >= localMaximum)
            {
                localMaximum = current;
                maximumIndex = i;
            }
        }
        peakValues[(size_t) region + 1] = localMaximum;
        peakIndices[(size_t) region + 1] = maximumIndex;
    }

    for (int point = 1; point <= regionCount + 1; ++point)
    {
        const int start = peakIndices[(size_t) point - 1];
        const int end = peakIndices[(size_t) point];
        const float startValue = peakValues[(size_t) point - 1];
        const float endValue = peakValues[(size_t) point];
        if (end <= start)
        {
            if (start >= 0 && start < envelopeBins)
                interpolated[(size_t) start] = endValue;
            continue;
        }
        for (int i = start; i < end && i < envelopeBins; ++i)
        {
            const float t = (float) (i - start) / (float) (end - start);
            interpolated[(size_t) i] = startValue + t * (endValue - startValue);
        }
    }
    interpolated[(size_t) envelopeBins - 1] = peakValues[(size_t) regionCount + 1];

    const float ratio = std::pow (2.0f, juce::jlimit (-48.0f, 48.0f,
                                                       currentShiftCents / 100.0f) / 12.0f);
    // At a neutral shift the warp is a mathematical identity except in
    // envelope gaps (where the clamped gain would be 0.05), so the frame
    // passes through bit-exactly. This is what makes the default insert
    // null-transparent; any non-zero shift uses the full envelope warp.
    if (std::abs (currentShiftCents) >= 0.005f)
    {
        for (int k = 0; k < numBins; ++k)
        {
            const float source = juce::jlimit (0.0f, (float) (envelopeBins - 1), k / ratio);
            const int lo = (int) source;
            const int hi = juce::jmin (lo + 1, envelopeBins - 1);
            const float frac = source - (float) lo;
            const float shifted = interpolated[(size_t) lo] * (1.0f - frac)
                                + interpolated[(size_t) hi] * frac;
            if (channel == 0 && k < envelopeBins)
                shiftedEnvelope[(size_t) k] = shifted;
            const float originalEnvelope = juce::jmax (interpolated[(size_t) juce::jmin (k, envelopeBins - 1)],
                                                       1.0e-9f);
            const float gain = juce::jlimit (0.05f, 20.0f,
                                             shifted / originalEnvelope);
            if (k == 0)
            {
                data[0] *= gain;
            }
            else if (k == numBins - 1)
            {
                data[1] *= gain;
            }
            else
            {
                data[2 * k] *= gain;
                data[2 * k + 1] *= gain;
            }
        }
    }
    else if (channel == 0)
    {
        // Neutral shift: the unshifted envelope is what the UI shows.
        std::copy (interpolated.begin(), interpolated.begin() + envelopeBins,
                   shiftedEnvelope.begin());
    }
    fft.performRealOnlyInverseTransform (data);
    window.multiplyWithWindowingTable (data, (size_t) fftSize);
    // Overlap-add normalisation: for the mean-normalised Hann window the
    // squared-window sum across the 4 overlapping grains is
    // Σw² = 4 x 1.5 = 6, so dividing by 6 reconstructs the input exactly
    // at ratio 1 (transparent on a default insert).
    for (int i = 0; i < fftSize; ++i)
        output[(outputIndex + i) % fftSize] += data[i] / (overlap * 1.5f);

    if (channel == 0)
    {
        // Publish the post-effect spectrum (what the frame turns into after
        // the envelope warp) and the shifted envelope for the UI.
        postMagnitudes[(size_t) 0] = std::abs (data[0]) + 1.0e-9f;
        postMagnitudes[(size_t) (numBins - 1)] = std::abs (data[1]) + 1.0e-9f;
        for (int k = 1; k < numBins - 1; ++k)
        {
            const float re = data[2 * k], im = data[2 * k + 1];
            postMagnitudes[(size_t) k] = std::sqrt (re * re + im * im) + 1.0e-9f;
        }

        const uint32_t next = (spectrumLatest.load (std::memory_order_relaxed) + 1u) % 3u;
        std::copy (postMagnitudes.begin(), postMagnitudes.begin() + numBins,
                   spectrumSlots[(size_t) next].begin());
        std::copy (shiftedEnvelope.begin(), shiftedEnvelope.begin() + envelopeBins,
                   envelopeSlots[(size_t) next].begin());
        envelopeSlots[(size_t) next][(size_t) (numBins - 1)]
            = shiftedEnvelope[(size_t) (envelopeBins - 1)];
        spectrumLatest.store (next, std::memory_order_release);
        frameCounter.store (frameCounter.load (std::memory_order_relaxed) + 1u,
                            std::memory_order_release);
        publishedWidth.store (width, std::memory_order_release);
        publishedBins.store (numBins, std::memory_order_release);
    }
}

void FormantShifterEngine::copyLatestSpectrum (float* destination, int count) const
{
    const uint32_t latest = spectrumLatest.load (std::memory_order_acquire);
    const auto& slot = spectrumSlots[(size_t) (latest % 3u)];
    std::copy (slot.begin(), slot.begin() + juce::jlimit (0, maxBins, count), destination);
}

void FormantShifterEngine::copyLatestEnvelope (float* destination, int count) const
{
    const uint32_t latest = spectrumLatest.load (std::memory_order_acquire);
    const auto& slot = envelopeSlots[(size_t) (latest % 3u)];
    std::copy (slot.begin(), slot.begin() + juce::jlimit (0, maxBins, count), destination);
}

} // namespace aki::dsp
