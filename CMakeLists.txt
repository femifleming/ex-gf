cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
    endif()
endif()

project(EXGF VERSION 1.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(EX_COPY_AFTER_BUILD "Install the plug-ins into your user plug-in folders after building" OFF)
option(EX_BUILD_TESTS "Build the offline test tool (EXTest)" ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE is missing - run: git submodule update --init")
endif()
add_subdirectory(JUCE)

# formats per platform
if(APPLE)
    set(EX_FORMATS AU VST3 Standalone)
elseif(UNIX)
    set(EX_FORMATS VST3 LV2 Standalone)
else()
    set(EX_FORMATS VST3 Standalone)
endif()

set(CORE_SOURCES Source/core/ExPlugin.cpp Source/core/ExEditor.cpp)
set(MI third_party/mi)
set(PLAITS_SOURCES ${MI}/plaits/dsp/fm/algorithms.cc ${MI}/plaits/dsp/fm/dx_units.cc ${MI}/stmlib/dsp/units.cc ${MI}/stmlib/utils/random.cc)
# the complete Plaits and Rings DSP (Macro Oscillator 2, Resonator)
file(GLOB PLAITS_ENGINE_SOURCES CONFIGURE_DEPENDS ${MI}/plaits/dsp/*.cc ${MI}/plaits/dsp/engine/*.cc ${MI}/plaits/dsp/engine2/*.cc
     ${MI}/plaits/dsp/chords/*.cc ${MI}/plaits/dsp/physical_modelling/*.cc ${MI}/plaits/dsp/speech/*.cc)
set(PLAITS_FULL_SOURCES ${PLAITS_SOURCES} ${PLAITS_ENGINE_SOURCES} ${MI}/plaits/resources.cc)
file(GLOB RINGS_DSP_SOURCES CONFIGURE_DEPENDS ${MI}/rings/dsp/*.cc)
set(RINGS_SOURCES ${RINGS_DSP_SOURCES} ${MI}/rings/resources.cc)
set(STMLIB_SOURCES ${MI}/stmlib/dsp/units.cc ${MI}/stmlib/utils/random.cc)

# TEST makes stmlib use portable C++ instead of ARM assembly
set(EX_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
            JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0 TEST=1 _USE_MATH_DEFINES=1 NOMINMAX=1)

# kind: INSTRUMENT (default) | MIDIFX (effect played from MIDI) | FXMIDI (insert effect that also listens to MIDI) | FX (insert effect)
function(ex_plugin target product code desc sources midiOut)
    set(kind INSTRUMENT)
    if(ARGC GREATER 6)
        set(kind ${ARGV6})
    endif()
    set(isSynth TRUE)
    set(midiIn TRUE)
    set(auType kAudioUnitType_MusicDevice)
    set(vst3Cats Instrument Synth)
    set(micText "Audio input is used to record and auto-sample instruments.")
    if(kind STREQUAL "MIDIFX")
        set(isSynth FALSE)
        set(auType kAudioUnitType_MusicEffect)
        set(vst3Cats Fx)
    elseif(kind STREQUAL "FXMIDI")
        set(isSynth FALSE)
        set(auType kAudioUnitType_Effect)
        set(vst3Cats Fx)
    elseif(kind STREQUAL "FX")
        set(isSynth FALSE)
        set(midiIn FALSE)
        set(auType kAudioUnitType_Effect)
        set(vst3Cats Fx)
    endif()
    if(NOT isSynth)
        set(micText "Audio input is processed by the effect.")
    endif()
    juce_add_plugin(${target}
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "FreakiePie"
        COMPANY_WEBSITE "https://github.com/femifleming/ex-gf"
        BUNDLE_ID "com.freakiepie.${target}"
        PLUGIN_MANUFACTURER_CODE Frkp
        PLUGIN_CODE ${code}
        FORMATS ${EX_FORMATS}
        PRODUCT_NAME "${product}"
        DESCRIPTION "${desc}"
        ICON_BIG Resources/icons/${target}.png
        IS_SYNTH ${isSynth}
        NEEDS_MIDI_INPUT ${midiIn}
        NEEDS_MIDI_OUTPUT ${midiOut}
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        AU_MAIN_TYPE ${auType}
        VST3_CATEGORIES ${vst3Cats}
        LV2URI "https://github.com/femifleming/ex-gf/${target}"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "${micText}"
        COPY_PLUGIN_AFTER_BUILD ${EX_COPY_AFTER_BUILD})
    target_sources(${target} PRIVATE ${CORE_SOURCES} ${sources})
    target_include_directories(${target} PRIVATE Source ${MI})
    target_compile_definitions(${target} PUBLIC ${EX_DEFS})
    target_link_libraries(${target} PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endfunction()

ex_plugin(EXMultisample "EX Multisample" ExMs "SD Multisample + WAV Recorder auto-sampler"
    "Source/plugins/MultisamplePlugin.cpp;Source/algorithms/SdMultisample.cpp;Source/algorithms/WavRecorder.cpp" TRUE)
ex_plugin(EXPolyWavetable "EX Poly Wavetable" ExWt "16 voice wavetable synthesizer"
    "Source/plugins/PolyWavetablePlugin.cpp;Source/algorithms/PolyWavetable.cpp" FALSE)
ex_plugin(EXPolyFM "EX Poly FM" ExFm "16 voice 4 timbre DX7-compatible FM synthesizer"
    "Source/plugins/PolyFMPlugin.cpp;Source/algorithms/PolyFM.cpp;Source/algorithms/PlaitsBanks.cpp;${PLAITS_SOURCES}" TRUE)
ex_plugin(EXPolyExciter "EX Poly Exciter" ExEx "16 voice exciter/resonator synthesizer"
    "Source/plugins/PolyExciterPlugin.cpp;Source/algorithms/PolyExciter.cpp" FALSE)
ex_plugin(EXGranulator "EX Granulator" ExGr "Granular synthesis from live input or WAV files"
    "Source/plugins/GranulatorPlugin.cpp;Source/algorithms/Granulator.cpp" FALSE MIDIFX)
ex_plugin(EXMultiFX "EX Multi-FX" ExMx "EQ, chorus/phaser/pitch shift, delay and reverb with variable sample rate"
    "Source/plugins/MultiFXPlugin.cpp;Source/algorithms/MultiFX.cpp" FALSE FX)
ex_plugin(EXResonator "EX Resonator" ExRs "Modal and string resonator (Rings)"
    "Source/plugins/ResonatorPlugin.cpp;Source/algorithms/Resonator.cpp;${RINGS_SOURCES};${STMLIB_SOURCES}" FALSE MIDIFX)
ex_plugin(EXSpectralFreeze "EX Spectral Freeze" ExSf "Four voice spectral freeze"
    "Source/plugins/SpectralFreezePlugin.cpp;Source/algorithms/SpectralFreeze.cpp" FALSE FXMIDI)
ex_plugin(EXMacroOsc2 "EX Macro Oscillator 2" ExMo "Four macro oscillators (Plaits)"
    "Source/plugins/MacroOsc2Plugin.cpp;Source/algorithms/MacroOsc2.cpp;${PLAITS_FULL_SOURCES}" FALSE)

if(EX_BUILD_TESTS)
    juce_add_console_app(EXTest PRODUCT_NAME "EXTest")
    target_sources(EXTest PRIVATE tests/EXTest.cpp ${CORE_SOURCES}
        Source/algorithms/SdMultisample.cpp Source/algorithms/WavRecorder.cpp Source/algorithms/PolyWavetable.cpp
        Source/algorithms/PolyFM.cpp Source/algorithms/PlaitsBanks.cpp Source/algorithms/PolyExciter.cpp
        Source/algorithms/Granulator.cpp Source/algorithms/MultiFX.cpp Source/algorithms/Resonator.cpp
        Source/algorithms/SpectralFreeze.cpp Source/algorithms/MacroOsc2.cpp ${PLAITS_FULL_SOURCES} ${RINGS_SOURCES})
    target_include_directories(EXTest PRIVATE Source ${MI})
    target_compile_definitions(EXTest PRIVATE ${EX_DEFS} JucePlugin_Name="EXTest" JUCE_STANDALONE_APPLICATION=1
        EX_TEST_BUILD=1 EX_FULL_PLAITS_RESOURCES=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_PLUGINHOST_VST3=1 $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>)
    target_link_libraries(EXTest PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
endif()
