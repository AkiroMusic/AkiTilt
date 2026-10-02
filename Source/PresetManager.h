#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

#include "Parameters.h"

namespace aki::presets
{

// Factory presets, ordered from subtle to extreme. Each touches the effect
// parameters plus a character-matched FFT analysis size (smooth timbres get
// large windows, snappy ones small); Pitch, Out, Limiter and Auto Formant
// always stay at the user's own values.
struct FactoryPreset
{
    const char* name;
    float formantShift, bands, tilt;
    float dryWet, width, airAmount, airTone;
    int fftSize;
};

// Choice-parameter index (0..5) for an analysis size in points; anything not
// a power of two snaps to the nearest window.
inline int fftIndexFromSize (int points)
{
    const double log2Size = std::log2 ((double) juce::jlimit (128, 4096, points));
    return juce::jlimit (0, 5, (int) std::round (log2Size) - 7);
}

inline const std::vector<FactoryPreset>& getAll()
{
    static const std::vector<FactoryPreset> presets = {
        { "Init",               0.0f,  10.0f,  0.0f, 100.0f, 100.0f,  0.0f, 50.0f, 2048 },
        { "Guitar Bloom",     180.0f,   5.5f,  1.5f, 100.0f, 100.0f,  0.0f, 50.0f, 1024 },
        { "Tape Bloom",      -120.0f,  12.0f, -3.0f, 100.0f, 100.0f, 25.0f, 25.0f, 4096 },
        { "Gender Morph M>F", 320.0f,   8.0f,  1.0f, 100.0f, 115.0f,  8.0f, 55.0f, 2048 },
        { "Gender Morph F>M", -340.0f,  11.0f, -1.0f, 100.0f, 115.0f,  8.0f, 45.0f, 2048 },
        { "Fairy Choir",      520.0f,   6.5f,  2.0f, 100.0f, 160.0f, 20.0f, 70.0f, 4096 },
        { "Petalcore Lead",   650.0f,   4.5f,  2.5f, 100.0f, 140.0f, 10.0f, 70.0f,  512 },
        { "Chipmunk Garden",  900.0f,   5.0f,  2.0f, 100.0f, 120.0f, 10.0f, 75.0f,  256 },
        { "Deep Oracle",     -600.0f,  13.0f, -2.0f, 100.0f, 130.0f, 12.0f, 40.0f, 4096 },
    };
    return presets;
}

// User presets are plain state XML files in a per-user folder, so they can
// also be added, renamed, backed up or shared straight from the OS file
// manager.
inline juce::File userPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("AkiTilt")
        .getChildFile ("Presets");
}

inline juce::String sanitiseName (juce::String name)
{
    return name.removeCharacters ("\\/:*?\"<>|").trim();
}

struct PresetEntry
{
    juce::String name;
    bool isFactory = false;
    int factoryIndex = -1;
    juce::File file;
};

inline std::vector<PresetEntry> scanPresets (const juce::File& userDirectory = userPresetDirectory())
{
    std::vector<PresetEntry> entries;
    for (size_t i = 0; i < getAll().size(); ++i)
        entries.push_back ({ getAll()[i].name, true, (int) i, {} });

    juce::Array<juce::File> files;
    if (userDirectory.isDirectory())
        userDirectory.findChildFiles (files, juce::File::findFiles, false, "*.xml");
    juce::File::NaturalFileComparator comparator (false);
    files.sort (comparator);
    for (const auto& f : files)
        entries.push_back ({ f.getFileNameWithoutExtension(), false, -1, f });
    return entries;
}

inline bool writeStateFile (juce::ValueTree state, const juce::String& name, const juce::File& directory)
{
    const auto clean = sanitiseName (name);
    if (clean.isEmpty())
        return false;
    directory.createDirectory();
    state.setProperty ("presetName", clean, nullptr);
    state.setProperty ("presetVersion", 1, nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    return xml != nullptr && xml->writeTo (directory.getChildFile (clean + ".xml"));
}

inline juce::ValueTree readStateFile (const juce::File& file)
{
    const auto xml = juce::parseXML (file);
    return xml != nullptr ? juce::ValueTree::fromXml (*xml) : juce::ValueTree();
}

inline bool saveUserPreset (juce::AudioProcessorValueTreeState& apvts, const juce::String& name)
{
    return writeStateFile (apvts.copyState(), name, userPresetDirectory());
}

inline void apply (juce::AudioProcessorValueTreeState& apvts, int index)
{
    const auto& presets = getAll();
    if (index < 0 || index >= (int) presets.size())
        return;
    const auto& preset = presets[(size_t) index];

    auto set = [&apvts] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set (aki::params::formantShift, preset.formantShift);
    set (aki::params::bands, preset.bands);
    set (aki::params::tilt, preset.tilt);
    set (aki::params::dryWet, preset.dryWet);
    set (aki::params::width, preset.width);
    set (aki::params::airAmount, preset.airAmount);
    set (aki::params::airTone, preset.airTone);
    // The FFT size is a choice parameter whose value domain is the item index.
    if (auto* param = apvts.getParameter (aki::params::fftSize))
        param->setValueNotifyingHost (param->convertTo0to1 (
            (float) fftIndexFromSize (preset.fftSize)));
}

inline void applyFile (juce::AudioProcessorValueTreeState& apvts, const juce::File& file)
{
    auto tree = readStateFile (file);
    if (tree.hasType ("PARAMETERS"))
        apvts.replaceState (tree);
}

} // namespace aki::presets
