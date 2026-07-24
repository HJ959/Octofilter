cmake_minimum_required(VERSION 3.15)
project(Octofilter VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── DPF ───────────────────────────────────────────────────────────────────────
# DPF must be added before dpf_add_plugin is called.
add_subdirectory(deps/dpf)

# ── pffft ─────────────────────────────────────────────────────────────────────
add_subdirectory(deps/pffft)

# ── Plugin targets ────────────────────────────────────────────────────────────
# dpf_add_plugin creates VST3, CLAP (and AU on macOS) targets automatically.
# AU is only built on macOS; DPF handles that conditionality internally.
dpf_add_plugin(Octofilter
    TARGETS vst3 clap au
    FILES_DSP
        src/OctofilterPlugin.cpp
    FILES_UI
        ui/OctofilterUI.cpp
)

target_include_directories(Octofilter
    PUBLIC
        src/
        deps/pffft/
        ui/
)

target_link_libraries(Octofilter
    PRIVATE
        pffft
)

# ── Unit tests ────────────────────────────────────────────────────────────────
enable_testing()

add_executable(OctofilterTests
    tests/test_dsp.cpp
)

target_include_directories(OctofilterTests
    PRIVATE
        src/
        deps/
        deps/pffft/
)

target_compile_features(OctofilterTests PRIVATE cxx_std_17)

target_link_libraries(OctofilterTests PRIVATE pffft)

# MSVC requires _USE_MATH_DEFINES for M_PI, M_SQRT2 etc.
if(MSVC)
    target_compile_definitions(Octofilter     PRIVATE _USE_MATH_DEFINES)
    target_compile_definitions(OctofilterTests PRIVATE _USE_MATH_DEFINES)
endif()

add_test(NAME DSPTests COMMAND OctofilterTests)
