cmake_minimum_required(VERSION 3.24)
project(AmpLab VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW ON)
FetchContent_Declare(nam
  GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
  GIT_TAG v0.6.0
  SOURCE_SUBDIR do_not_add_subdirectory)
FetchContent_MakeAvailable(JUCE nam)

# Libreria NAM compilata a parte (il repository non espone un target CMake pronto)
file(GLOB_RECURSE NAM_SOURCES CONFIGURE_DEPENDS ${nam_SOURCE_DIR}/NAM/*.cpp)
set(NAM_INCLUDES
  ${nam_SOURCE_DIR}
  ${nam_SOURCE_DIR}/Dependencies/eigen
  ${nam_SOURCE_DIR}/Dependencies/nlohmann
  ${nam_SOURCE_DIR}/Dependencies/nlohmann/include)
add_library(namcore STATIC ${NAM_SOURCES})
target_include_directories(namcore PUBLIC ${NAM_INCLUDES})
target_compile_features(namcore PUBLIC cxx_std_20)
target_compile_definitions(namcore PUBLIC NAM_SAMPLE_FLOAT)
set_target_properties(namcore PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Le architetture dei modelli (WaveNet, LSTM, SlimmableContainer...) si registrano da sole all'avvio.
# Con una libreria statica il linker scarta i file non richiamati: li forzo tutti (whole archive).
add_library(namcore_all INTERFACE)
target_include_directories(namcore_all INTERFACE ${NAM_INCLUDES})
target_compile_features(namcore_all INTERFACE cxx_std_20)
target_compile_definitions(namcore_all INTERFACE NAM_SAMPLE_FLOAT)
target_link_libraries(namcore_all INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,namcore>")

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
  PRIVATE namcore_all juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
