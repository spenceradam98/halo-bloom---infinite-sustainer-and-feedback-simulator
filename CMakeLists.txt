cmake_minimum_required(VERSION 3.22)

# macOS 11 (Big Sur) on Intel is the minimum; build universal (x86_64 + arm64).
# No -march flags anywhere: the DSP is scalar C++ and runs on any x86-64 (no AVX2 needed).
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT: no VC++ redist needed
endif()
project(HaloBloom VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Core engine (no JUCE dependency, unit-testable) ----
add_library(halo_engine STATIC Source/engine/Engine.cpp Source/engine/Scales.cpp Source/engine/Presets.cpp)
target_include_directories(halo_engine PUBLIC Source/engine)
set_target_properties(halo_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(HALO_BUILD_TESTS "Build engine tests" ON)
if(HALO_BUILD_TESTS)
    add_executable(halo_tests tests/EngineTests.cpp)
    target_link_libraries(halo_tests PRIVATE halo_engine)
    enable_testing()
    add_test(NAME engine COMMAND halo_tests)
endif()

option(HALO_BUILD_PLUGIN "Build the JUCE plugin" ON)
if(NOT HALO_BUILD_PLUGIN)
    return()
endif()

# ---- JUCE ----
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# VST3 + Standalone everywhere, AU on macOS. Standalone is for live guitar through an audio interface.
set(HALO_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND HALO_FORMATS AU)
endif()

juce_add_plugin(HaloBloom
    COMPANY_NAME "HaloBloomAudio"
    PLUGIN_MANUFACTURER_CODE Hlob
    PLUGIN_CODE Hlb1
    FORMATS ${HALO_FORMATS}
    PRODUCT_NAME "Halo Bloom"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_MusicEffect)

set(HALO_PLUGIN_SOURCES
    Source/plugin/PluginProcessor.cpp
    Source/plugin/PluginEditor.cpp
    Source/plugin/Themes.cpp)
target_sources(HaloBloom PRIVATE ${HALO_PLUGIN_SOURCES})

target_compile_definitions(HaloBloom PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(HaloBloom
    PRIVATE
        halo_engine
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- optional: render the editor in every theme to PNG (docs screenshots) ----
option(HALO_BUILD_SNAPSHOTS "Build the UI screenshot tool" OFF)
if(HALO_BUILD_SNAPSHOTS)
    juce_add_console_app(halo_snapshots PRODUCT_NAME "Halo Bloom Snapshots")
    target_sources(halo_snapshots PRIVATE tools/Snapshots.cpp ${HALO_PLUGIN_SOURCES})
    target_compile_definitions(halo_snapshots PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Halo Bloom"
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(halo_snapshots PRIVATE halo_engine juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
