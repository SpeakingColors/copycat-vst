cmake_minimum_required(VERSION 3.22)
project(copycat VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.16-security GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

juce_add_plugin(Copycat
    COMPANY_NAME "Studio 87"
    PLUGIN_MANUFACTURER_CODE Spco
    PLUGIN_CODE Cpct
    FORMATS VST3 Standalone
    PRODUCT_NAME "Copycat"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE)
target_sources(Copycat PRIVATE src/Plugins.cpp src/copycat/Copycat.cpp)
target_include_directories(Copycat PRIVATE src)
target_compile_definitions(Copycat PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Copycat PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
