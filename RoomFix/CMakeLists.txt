if(APPLE)
    set(ROOMFIX_FORMATS VST3 AU Standalone)
else()
    set(ROOMFIX_FORMATS VST3 Standalone)
endif()

juce_add_plugin(RoomFix2000
    PRODUCT_NAME "RoomFix 2000"
    COMPANY_NAME "AeroVox Audio"
    BUNDLE_ID com.aerovoxaudio.roomfix2000
    PLUGIN_MANUFACTURER_CODE Aerv
    PLUGIN_CODE Rfx2
    FORMATS ${ROOMFIX_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx EQ
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "RoomFix 2000 standalone can learn your room from the microphone.")

target_sources(RoomFix2000 PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(RoomFix2000 PRIVATE Source ${CMAKE_SOURCE_DIR}/Source)
target_compile_definitions(RoomFix2000 PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(RoomFix2000
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

if(AEROVOX_BUILD_TESTS)
    juce_add_console_app(RoomFixTest PRODUCT_NAME "RoomFixTest")
    target_sources(RoomFixTest PRIVATE tests/RoomFixTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(RoomFixTest PRIVATE Source ${CMAKE_SOURCE_DIR}/Source)
    target_compile_definitions(RoomFixTest PRIVATE
        JucePlugin_Name="RoomFix 2000" JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(RoomFixTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
