# deps/pffft/CMakeLists.txt
# Downloads pffft.c from the canonical mirror if not already present,
# then builds a small static library used by the Octofilter DSP.

cmake_minimum_required(VERSION 3.15)

set(PFFFT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/pffft.c")

if(NOT EXISTS "${PFFFT_SRC}")
    message(STATUS "Downloading pffft.c...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/rtoy/pffft/master/pffft.c"
        "${PFFFT_SRC}"
        SHOW_PROGRESS
        STATUS DOWNLOAD_STATUS
    )
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download pffft.c. "
            "Manually place it at deps/pffft/pffft.c")
    endif()
endif()

add_library(pffft STATIC "${PFFFT_SRC}" "${CMAKE_CURRENT_SOURCE_DIR}/pffft.h")
target_include_directories(pffft PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# _USE_MATH_DEFINES is required on MSVC to get M_PI, M_SQRT2 etc.
target_compile_definitions(pffft PRIVATE _USE_MATH_DEFINES)

# Enable SIMD on supported platforms (GCC/Clang only — MSVC enables SSE2 automatically on x64)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
        target_compile_options(pffft PRIVATE -msse2)
    endif()
endif()

# Suppress warnings in vendored code
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(pffft PRIVATE -w)
endif()
