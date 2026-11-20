cmake_minimum_required(VERSION 3.22)

project(SilverPhase VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: baixado automaticamente pelo GitHub Actions.
# (Para compilar local com uma copia do JUCE: -DJUCE_LOCAL_PATH=C:/caminho/JUCE)
if(DEFINED JUCE_LOCAL_PATH)
    add_subdirectory(${JUCE_LOCAL_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(SilverPhase
    COMPANY_NAME                "SILVER"
    PLUGIN_MANUFACTURER_CODE    Slvr
    PLUGIN_CODE                 SlPh
    FORMATS                     VST3
    PRODUCT_NAME                "SilverPhase"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             "Fx" "Analyzer"
)

target_sources(SilverPhase PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/CorrelationAnalyzer.cpp
    Source/PhaseCorrector.cpp
)

target_include_directories(SilverPhase PRIVATE Source)

target_compile_definitions(SilverPhase PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# Acentos nos textos da interface
target_compile_options(SilverPhase PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(SilverPhase
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
