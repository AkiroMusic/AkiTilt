#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <BinaryData.h>

namespace aki::theme
{

// Mint Fresh palette — translated verbatim from Aki-Design-System.md §3.4
// and Appendix B ([data-theme='mint']).
namespace colours
{
inline const juce::Colour bgBase          { 0xffE8F0E5 };
inline const juce::Colour surface1        { 0xffF4F8F0 };
inline const juce::Colour surface2        { 0xffDCE7DA };
inline const juce::Colour contrast        { 0xff22362A };
inline const juce::Colour border          { 0xffC9D8C6 };
inline const juce::Colour textPrimary     { 0xff2F4A3A };
inline const juce::Colour textSecondary   { 0xff6B7268 };
inline const juce::Colour textTertiary    { 0xff98A69A };
inline const juce::Colour onContrast      { 0xffE8F0E5 };
inline const juce::Colour onContrastStrong{ 0xE6E8F0E5 };
inline const juce::Colour onContrastWeak  { 0xa3E8F0E5 };
inline const juce::Colour onContrastHair  { 0x12E8F0E5 };
inline const juce::Colour accent          { 0xff3D624C };
inline const juce::Colour accentHover     { 0xff32513F };
inline const juce::Colour accentSecondary { 0xff8FA89A };
inline const juce::Colour accentTertiary  { 0xffD9A38E };
inline const juce::Colour gradA           { 0xff8FA89A };
inline const juce::Colour gradB           { 0xffA3C4A9 };
inline const juce::Colour gradC           { 0xffB5D7C3 };
inline const juce::Colour success         { 0xff4E8F68 };
inline const juce::Colour error           { 0xffC4584E };
inline const juce::Colour innerShade      { 0x142F3A32 };
inline const juce::Colour bezelInner      { 0x0A000000 };
inline const juce::Colour shadowTint      { 0x1A2F3A32 };
}

// Three-font system: Plus Jakarta Sans (UI), Fraunces (display), IBM Plex
// Mono (numeric readouts). All OFL-licensed, bundled as static TTFs.
struct Fonts
{
    juce::Typeface::Ptr sansRegular, sansMedium, sansSemiBold, sansBold;
    juce::Typeface::Ptr displaySemiBold;
    juce::Typeface::Ptr monoRegular, monoMedium, monoSemiBold;
};

inline const Fonts& fonts()
{
    static const Fonts f = []()
    {
        auto make = [] (const char* data, int size)
        { return juce::Typeface::createSystemTypefaceFor (data, size); };
        return Fonts {
            make (BinaryData::PJSRegular_ttf, BinaryData::PJSRegular_ttfSize),
            make (BinaryData::PJSMedium_ttf, BinaryData::PJSMedium_ttfSize),
            make (BinaryData::PJSSemiBold_ttf, BinaryData::PJSSemiBold_ttfSize),
            make (BinaryData::PJSBold_ttf, BinaryData::PJSBold_ttfSize),
            make (BinaryData::Fraunces72SemiBold_ttf, BinaryData::Fraunces72SemiBold_ttfSize),
            make (BinaryData::PlexMonoRegular_ttf, BinaryData::PlexMonoRegular_ttfSize),
            make (BinaryData::PlexMonoMedium_ttf, BinaryData::PlexMonoMedium_ttfSize),
            make (BinaryData::PlexMonoSemiBold_ttf, BinaryData::PlexMonoSemiBold_ttfSize),
        };
    }();
    return f;
}

inline juce::Font fontFrom (juce::Typeface::Ptr face, float height)
{
    return juce::Font (juce::FontOptions().withTypeface (std::move (face)).withHeight (height));
}

inline juce::Font sans (float height)        { return fontFrom (fonts().sansRegular, height); }
inline juce::Font sansMedium (float height)  { return fontFrom (fonts().sansMedium, height); }
inline juce::Font sansSemiBold (float height){ return fontFrom (fonts().sansSemiBold, height); }
inline juce::Font display (float height)     { return fontFrom (fonts().displaySemiBold, height); }
inline juce::Font mono (float height)        { return fontFrom (fonts().monoRegular, height); }
inline juce::Font monoMedium (float height)  { return fontFrom (fonts().monoMedium, height); }

} // namespace aki::theme
