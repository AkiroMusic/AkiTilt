cmake_minimum_required(VERSION 3.24)

project(AkiTilt VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libs/JUCE)

juce_add_binary_data(AkiTiltAssets
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Assets/fonts/PJS-Regular.ttf
        Assets/fonts/PJS-Medium.ttf
        Assets/fonts/PJS-SemiBold.ttf
        Assets/fonts/PJS-Bold.ttf
        Assets/fonts/Fraunces72-SemiBold.ttf
        Assets/fonts/PlexMono-Regular.ttf
        Assets/fonts/PlexMono-Medium.ttf
        Assets/fonts/PlexMono-SemiBold.ttf)

# macOS additionally builds the AudioUnit.
if (APPLE)
    set(AKITILT_FORMATS VST3 AU Standalone)
else()
    set(AKITILT_FORMATS VST3 Standalone)
endif()

juce_add_plugin(AkiTilt
    PRODUCT_NAME "AkiTilt"
    COMPANY_NAME "Akiro"
    BUNDLE_ID "com.akiro.akitilt"
    PLUGIN_MANUFACTURER_CODE Akr0
    PLUGIN_CODE Aktl
    FORMATS ${AKITILT_FORMATS}
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(AkiTilt PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/AkiLookAndFeel.cpp
    Source/UI/XYPad.cpp
    Source/DSP/FormantShifterEngine.cpp)

target_include_directories(AkiTilt PRIVATE Source)

target_compile_definitions(AkiTilt PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AkiTilt
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        AkiTiltAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Keep a ready-to-test copy of the binaries in <root>/release. macOS targets
# are bundle *directories* (.vst3/.component/.app); cmake -E copy_directory
# copies a directory's CONTENTS, so each bundle needs its named destination,
# and copy_if_different cannot take a directory at all.
if (APPLE)
    add_custom_command(TARGET AkiTilt_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/release
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            $<TARGET_FILE:AkiTilt_VST3> ${CMAKE_SOURCE_DIR}/release/AkiTilt.vst3)
    if (TARGET AkiTilt_AU)
        add_custom_command(TARGET AkiTilt_AU POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/release
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                $<TARGET_FILE:AkiTilt_AU> ${CMAKE_SOURCE_DIR}/release/AkiTilt.component)
    endif()
    if (TARGET AkiTilt_Standalone)
        add_custom_command(TARGET AkiTilt_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/release
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                $<TARGET_FILE:AkiTilt_Standalone> ${CMAKE_SOURCE_DIR}/release/AkiTilt.app)
    endif()
else()
    add_custom_command(TARGET AkiTilt_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/release
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:AkiTilt_VST3> ${CMAKE_SOURCE_DIR}/release/)
    if (TARGET AkiTilt_Standalone)
        add_custom_command(TARGET AkiTilt_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/release
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:AkiTilt_Standalone> ${CMAKE_SOURCE_DIR}/release/)
    endif()
endif()

juce_add_console_app(AkiTiltTests PRODUCT_NAME "AkiTiltTests")

target_sources(AkiTiltTests PRIVATE
    Tests/EngineTests.cpp
    Source/DSP/FormantShifterEngine.cpp)

target_include_directories(AkiTiltTests PRIVATE Source)

target_compile_definitions(AkiTiltTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(AkiTiltTests
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
