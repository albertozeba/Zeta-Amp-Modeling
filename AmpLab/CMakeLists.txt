cmake_minimum_required(VERSION 3.22)
project(AmpLab VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(AmpLab
  COMPANY_NAME "AmpLab"
  PLUGIN_MANUFACTURER_CODE Amlb
  PLUGIN_CODE Amp1
  FORMATS Standalone VST3 AU
  PRODUCT_NAME "Amp Lab"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "Amp Lab elabora l'audio in ingresso (la chitarra).")

target_sources(AmpLab PRIVATE
  Source/PluginProcessor.cpp Source/PluginProcessor.h
  Source/PluginEditor.cpp Source/PluginEditor.h)

target_compile_definitions(AmpLab PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(AmpLab
  PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
