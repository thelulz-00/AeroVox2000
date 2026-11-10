cmake_minimum_required(VERSION 3.22)

project(AeroVox2000 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: link the C++ runtime statically so users don't need the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE: use a local copy if given (-DJUCE_PATH=...), otherwise download it
if(DEFINED JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(AEROVOX_FORMATS VST3 AU Standalone)
else()
    set(AEROVOX_FORMATS VST3 Standalone)
endif()

juce_add_plugin(AeroVox2000
    PRODUCT_NAME "AeroVox 2000"
    COMPANY_NAME "AeroVox Audio"
    BUNDLE_ID com.aerovoxaudio.aerovox2000
    PLUGIN_MANUFACTURER_CODE Aerv
    PLUGIN_CODE Av2k
    FORMATS ${AEROVOX_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE          # MIDI notes play the vocoder's internal synth
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    VST3_CATEGORIES Fx Reverb EQ
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "AeroVox 2000 standalone uses the microphone as vocoder input.")

target_sources(AeroVox2000 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(AeroVox2000 PRIVATE Source)

target_compile_definitions(AeroVox2000 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AeroVox2000
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# Optional test host (loads the built VST3, renders audio, checks the DSP)
option(AEROVOX_BUILD_TESTS "Build the offline test host" OFF)
if(AEROVOX_BUILD_TESTS)
    juce_add_console_app(AeroVoxTestHost PRODUCT_NAME "AeroVoxTestHost")
    target_sources(AeroVoxTestHost PRIVATE tests/TestHost.cpp)
    target_compile_definitions(AeroVoxTestHost PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AeroVoxTestHost PRIVATE
        juce::juce_audio_utils juce::juce_recommended_config_flags)

    juce_add_console_app(AeroVoxSnapshot PRODUCT_NAME "AeroVoxSnapshot")
    target_sources(AeroVoxSnapshot PRIVATE tests/Snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(AeroVoxSnapshot PRIVATE Source)
    target_compile_definitions(AeroVoxSnapshot PRIVATE
        JucePlugin_Name="AeroVox 2000" JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AeroVoxSnapshot PRIVATE
        juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
add_subdirectory(RoomFix)
