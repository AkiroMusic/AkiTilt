#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
float toDb (float sample)
{
    return juce::Decibels::gainToDecibels (std::abs (sample), -60.0f);
}
} // namespace

AkiTiltAudioProcessor::AkiTiltAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", aki::params::createParameterLayout())
{
    formantParam = parameters.getRawParameterValue (aki::params::formantShift);
    bandsParam = parameters.getRawParameterValue (aki::params::bands);
    pitchParam = parameters.getRawParameterValue (aki::params::pitchShift);
    tiltParam = parameters.getRawParameterValue (aki::params::tilt);
    mixParam = parameters.getRawParameterValue (aki::params::dryWet);
    widthParam = parameters.getRawParameterValue (aki::params::width);
    airParam = parameters.getRawParameterValue (aki::params::airAmount);
    toneParam = parameters.getRawParameterValue (aki::params::airTone);
    outGainParam = parameters.getRawParameterValue (aki::params::outputGain);
    limiterParam = parameters.getRawParameterValue (aki::params::limiter);
    bypassParam = parameters.getRawParameterValue (aki::params::bypass);
    fftSizeParam = parameters.getRawParameterValue (aki::params::fftSize);
    formantModeParam = parameters.getRawParameterValue (aki::params::formantMode);
}

void AkiTiltAudioProcessor::prepareToPlay (double sampleRate, int)
{
    // The engine is always prepared for both channels: hosts may call
    // prepareToPlay before the stereo layout settles, and a mono preparation
    // would leave channel 1 silent once stereo blocks arrive. Per-block we
    // still loop over the actual channel count, so mono buses behave the same.
    engine.prepare (sampleRate, 2);
    pitchShifter.prepare (sampleRate, 2);
    tiltEQ.prepare (sampleRate, 2);
    noiseBed.prepare (sampleRate, 2);
    stereoWidth.prepare (sampleRate);
    pitchDetector.prepare (sampleRate);
    dryDelay.prepare (aki::dsp::FormantShifterEngine::maxFftSize, 2);
    mixSmoothed.reset (sampleRate, 0.02);
    outGainSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.setCurrentAndTargetValue (mixParam->load() / 100.0f);
    outGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outGainParam->load()));
    airFollower = 0.0f;
    airReleaseCoef = (float) std::exp (-1.0 / (0.45 * sampleRate));
    // Restore the FFT size the user picked; the dry path and the reported
    // latency follow it. The STFT wet path runs fftSize samples behind the
    // input; tell the host and keep the dry path in lockstep so the
    // crossfade cannot comb.
    appliedFftIndex = juce::jlimit (0, aki::dsp::FormantShifterEngine::numFftOrders - 1,
                                    (int) fftSizeParam->load());
    engine.setFftOrder (aki::dsp::FormantShifterEngine::minFftOrder + appliedFftIndex);
    dryDelay.setDelay (engine.getFftSize());
    setLatencySamples (engine.getFftSize());
}

void AkiTiltAudioProcessor::releaseResources() {}

bool AkiTiltAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && in == out;
}

void AkiTiltAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin (2, getTotalNumInputChannels());

    const bool bypassed = bypassParam->load() > 0.5f;
    engine.setTargets (formantParam->load(), bandsParam->load());
    pitchShifter.setSemitones (pitchParam->load());
    tiltEQ.setTiltDb (tiltParam->load());
    tiltEQ.update (numSamples);
    stereoWidth.setWidth (widthParam->load() / 100.0f);
    noiseBed.setAmount (airParam->load() / 100.0f);
    noiseBed.setTone (toneParam->load() / 100.0f);
    noiseBed.update (numSamples, airFollower);
    mixSmoothed.setTargetValue (mixParam->load() / 100.0f);
    outGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outGainParam->load()));
    const bool limiterOn = limiterParam->load() > 0.5f;
    const bool autoFormant = formantModeParam->load() > 0.5f;

    // Apply a pending FFT-size change at the block boundary. Every per-order
    // resource is preallocated in the engine, so this never allocates; the
    // wet stream restarts from silence and the dry path re-aligns with it.
    const int fftChoice = juce::jlimit (0, aki::dsp::FormantShifterEngine::numFftOrders - 1,
                                        (int) fftSizeParam->load());
    if (fftChoice != appliedFftIndex)
    {
        appliedFftIndex = fftChoice;
        engine.setFftOrder (aki::dsp::FormantShifterEngine::minFftOrder + fftChoice);
        dryDelay.setDelay (engine.getFftSize());
        setLatencySamples (engine.getFftSize());
    }

    float inPeak = 0.0f, outPeak = 0.0f;
    float detectedCents = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float dry[2] = { 0.0f, 0.0f };
        float wet[2] = { 0.0f, 0.0f };
        float monoSum = 0.0f;

        for (int ch = 0; ch < channels; ++ch)
        {
            const float input = buffer.getSample (ch, sample);
            dry[ch] = dryDelay.processSample (ch, input);
            engine.writeInput (ch, input);
            monoSum += input;
            inPeak = juce::jmax (inPeak, std::abs (input));
        }
        if (channels > 1)
            monoSum *= 0.5f;
        pitchDetector.write (monoSum);
        detectedCents = pitchDetector.track();

        for (int ch = 0; ch < channels; ++ch)
            wet[ch] = engine.readOutput (ch);
        engine.advance();

        for (int ch = 0; ch < channels; ++ch)
        {
            wet[ch] = pitchShifter.processSample (ch, wet[ch]);
            wet[ch] = tiltEQ.processSample (ch, wet[ch]);
        }

        if (channels > 1)
            stereoWidth.processSample (wet[0], wet[1]);

        for (int ch = 0; ch < channels; ++ch)
            wet[ch] += noiseBed.processSample (ch);

        const float mix = mixSmoothed.getNextValue();
        const float gain = outGainSmoothed.getNextValue();

        for (int ch = 0; ch < channels; ++ch)
        {
            float out = dry[ch] * (1.0f - mix) + wet[ch] * mix;
            out *= gain;
            if (limiterOn)
                out = std::tanh (out);
            if (bypassed)
                out = dry[ch];
            outPeak = juce::jmax (outPeak, std::abs (out));
            buffer.setSample (ch, sample, out);
        }
        dryDelay.advance();
    }

    // In Auto mode the detected pitch (offset by the Formant knob) is what
    // drives the engine; the manual path at the top of the block already set
    // its targets. One block of detection lag hides inside the engine's own
    // 20 ms parameter smoothing.
    if (autoFormant)
    {
        const float effective = aki::dsp::autoFormantCents (detectedCents, formantParam->load());
        engine.setTargets (effective, bandsParam->load());
        effectiveFormantCents.store (effective, std::memory_order_release);
    }

    for (int ch = channels; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // Air-bed tracking envelope: instant attack on the block peak, ~450 ms
    // release (the per-sample coefficient raised to the block length, so the
    // fade time is independent of the host's buffer size). The bed gain
    // follows this envelope continuously, wrapping the sound in noise.
    const float blockRelease = std::pow (airReleaseCoef, (float) numSamples);
    airFollower = juce::jmax (inPeak, airFollower * blockRelease);

    inPeakDb.store (juce::jmax (toDb (inPeak), inPeakDb.load() - 0.6f));
    outPeakDb.store (juce::jmax (toDb (outPeak), outPeakDb.load() - 0.6f));
}

juce::AudioProcessorEditor* AkiTiltAudioProcessor::createEditor()
{
    return new aki::ui::AkiTiltAudioProcessorEditor (*this);
}

void AkiTiltAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, dest);
}

void AkiTiltAudioProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AkiTiltAudioProcessor();
}
