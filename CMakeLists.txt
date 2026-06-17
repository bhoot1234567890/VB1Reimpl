cmake_minimum_required(VERSION 3.22)
project(VB1Reimpl VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Point this at your JUCE checkout (clone https://github.com/juce-framework/JUCE) ----
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "$ENV{JUCE_DIR}" CACHE PATH "Path to the JUCE source tree")
endif()
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Set JUCE_DIR to a JUCE checkout, e.g.:\n"
        "  git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE\n"
        "  cmake -B build -DJUCE_DIR=/path/to/JUCE")
endif()
add_subdirectory("${JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(VB1Reimpl
    COMPANY_NAME           "Personal"
    PLUGIN_MANUFACTURER_CODE Vb1r
    PLUGIN_CODE            Vb1x
    FORMATS                VST3 AU Standalone  # Standalone = double-click app, no DAW needed
    PRODUCT_NAME           "VB-1 reimpl"
    IS_SYNTH               TRUE
    NEEDS_MIDI_INPUT       TRUE
    MIDI_INPUT             TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE            # copies into ~/Library/Audio/Plug-Ins on macOS
    VST3_CATEGORIES        Instrument Synth)

target_sources(VB1Reimpl PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(VB1Reimpl PRIVATE Source)

target_compile_definitions(VB1Reimpl PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DEPRECATION_WARNINGS=0)

target_link_libraries(VB1Reimpl
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Headless renderer (Phase-4 A/B): renders the reimpl DSP to a WAV ----
juce_add_console_app(VB1Render)
target_sources(VB1Render PRIVATE tools/render_reimpl.cpp)
target_include_directories(VB1Render PRIVATE Source)
target_compile_definitions(VB1Render PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(VB1Render PRIVATE
    juce::juce_core juce::juce_events juce::juce_graphics juce::juce_gui_basics
    juce::juce_data_structures juce::juce_audio_basics juce::juce_audio_formats)
