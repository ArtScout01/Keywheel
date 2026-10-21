cmake_minimum_required(VERSION 3.22)

# macOS: Universal Binary (Apple Silicon + Intel). Muss VOR project() stehen.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()
# Windows: Runtime statisch einbinden, damit niemand die VC++-Redistributables installieren muss
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(ModeWheel VERSION 1.1.0)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

option(MW_COPY_AFTER_BUILD "Copy VST3 into the system plug-in folder after building" ON)

juce_add_plugin(ModeWheel
    COMPANY_NAME "WheelsDIY"
    PRODUCT_NAME "Mode Wheel"
    PLUGIN_MANUFACTURER_CODE Wdiy
    PLUGIN_CODE Mwhl
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD ${MW_COPY_AFTER_BUILD})

target_sources(ModeWheel PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ModeWheel PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ModeWheel
    PRIVATE juce::juce_audio_utils
            juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)
