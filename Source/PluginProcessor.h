#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/DelayLine.h"
#include "DSP/FormantShifterEngine.h"
#include "DSP/NoiseBed.h"
#include "DSP/PitchDetector.h"
#include "DSP/PitchShifter.h"
#include "DSP/StereoWidth.h"
#include "DSP/TiltEQ.h"

class AkiTiltAudioProcessor final : public juce::AudioProcessor
{
public:
    AkiTiltAudioProcessor();

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AkiTilt"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getParameterTree() { return parameters; }

    // Editor-facing live data.
    aki::dsp::FormantShifterEngine engine;
    std::atomic<float> inPeakDb { -60.0f };
    std::atomic<float> outPeakDb { -60.0f };
    // Live formant value the Auto mode feeds the engine with (cents); the
    // XY pad cursor tracks it while Auto is engaged.
    std::atomic<float> effectiveFormantCents { 0.0f };

private:
    juce::AudioProcessorValueTreeState parameters;

    std::atomic<float>* formantParam = nullptr;
    std::atomic<float>* bandsParam = nullptr;
    std::atomic<float>* pitchParam = nullptr;
    std::atomic<float>* tiltParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* airParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* outGainParam = nullptr;
    std::atomic<float>* limiterParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* fftSizeParam = nullptr;
    std::atomic<float>* formantModeParam = nullptr;

    aki::dsp::PitchShifter pitchShifter;
    aki::dsp::TiltEQ tiltEQ;
    aki::dsp::NoiseBed noiseBed;
    aki::dsp::StereoWidth stereoWidth;
    aki::dsp::PitchDetector pitchDetector;
    aki::dsp::DelayLine dryDelay;
    juce::SmoothedValue<float> mixSmoothed, outGainSmoothed;
    float airFollower = 0.0f;
    float airReleaseCoef = 0.999f;
    int appliedFftIndex = 4; // default choice index = 2048

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AkiTiltAudioProcessor)
};
