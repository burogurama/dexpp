cmake_minimum_required(VERSION 3.18)

project(dex++)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DEXPP_BUILD_PYTHON "Build the dexpp Python extension module" OFF)
option(DEXPP_BUILD_TESTS "Build the unit tests and demo CLI" ON)
option(DEXPP_BUILD_C "Build the C ABI shim (bindings/c, for Go/cgo and other FFI)" ON)
option(DEXPP_BUILD_FUZZERS "Build the libFuzzer harnesses in fuzz/ (Clang only)" OFF)
set(DEXPP_SANITIZE "" CACHE STRING
    "Sanitizers to build everything with, e.g. 'address,undefined' (empty = off)")

# SKBUILD is set by scikit-build-core during `pip install` / wheel builds:
# build only the Python module there, no tests or demo CLI.
if(SKBUILD)
    set(DEXPP_BUILD_PYTHON ON)
    set(DEXPP_BUILD_TESTS OFF)
endif()

# Sanitizer / fuzzer instrumentation is applied directory-wide (before any
# target is declared) so the library, the fetched test frameworks, and the
# harnesses are all built consistently.  libFuzzer needs coverage
# instrumentation (`-fsanitize=fuzzer-no-link`) in the LIBRARY objects, not
# just the harness, or it gets no coverage feedback from library code — so
# turning fuzzers on forces ASan+UBSan+coverage globally and the individual
# fuzz executables add the `fuzzer` (entry-point) part on top.
if(DEXPP_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "DEXPP_BUILD_FUZZERS requires Clang (libFuzzer).")
    endif()
    # Clang < 19 reports __cpp_concepts=201907, which guards out libstdc++'s
    # <expected> (it wants >= 202002); Clang 19 bumped the macro, so
    # std::expected works under plain libstdc++ — no libc++ needed, and Clang's
    # libstdc++-built libFuzzer runtime links cleanly.
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19)
        message(FATAL_ERROR "DEXPP_BUILD_FUZZERS requires Clang >= 19 "
                            "(earlier Clang cannot compile <expected> with libstdc++).")
    endif()
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined
                        -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
elseif(DEXPP_SANITIZE)
    add_compile_options(-fsanitize=${DEXPP_SANITIZE}
                        -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${DEXPP_SANITIZE})
endif()

include(FetchContent)

# libdeflate decompresses the APK's DEX entries.  Its decompression sources are
# compiled straight into dexpp_core rather than linked as a separate library, so
# libdexpp_core.a stays self-contained: the Go static link and the Python wheel
# need no extra system library.  SOURCE_SUBDIR points at a directory with no
# CMakeLists.txt so libdeflate's own build (and its targets) is never added.
FetchContent_Declare(
        libdeflate
        URL https://github.com/ebiggers/libdeflate/archive/refs/tags/v1.26.tar.gz
        URL_HASH SHA256=bba03fffc5538576213675ce6968fcff6ce2e67d82e4d5febea2d05f9f13cf85
        SOURCE_SUBDIR not-a-cmake-project
)
FetchContent_MakeAvailable(libdeflate)
set(LIBDEFLATE_SOURCES
    ${libdeflate_SOURCE_DIR}/lib/deflate_decompress.c
    ${libdeflate_SOURCE_DIR}/lib/utils.c
    ${libdeflate_SOURCE_DIR}/lib/x86/cpu_features.c
    ${libdeflate_SOURCE_DIR}/lib/arm/cpu_features.c
)
set_source_files_properties(${LIBDEFLATE_SOURCES} PROPERTIES COMPILE_OPTIONS "-std=c11")

add_library(dexpp_core STATIC
    src/raw/error.cpp
    src/raw/parser.cpp
    src/raw/types.cpp
    src/raw/parsing_utils.cpp
    src/raw/zip.cpp
    src/analysis/analysis_context.cpp
    src/analysis/class.cpp
    src/analysis/class_ref.cpp
    src/analysis/method.cpp
    src/analysis/field.cpp
    src/analysis/type_descriptor.cpp
    src/analysis/instruction.cpp
    src/analysis/cfg.cpp
    src/analysis/call_graph.cpp
    src/analysis/class_hierarchy.cpp
    src/analysis/xrefs.cpp
    src/analysis/annotations.cpp
    src/apk/string_pool.cpp
    src/apk/axml.cpp
    src/apk/resources.cpp
    src/apk/manifest.cpp
    src/apk/signing.cpp
    src/apk/apk.cpp
    ${LIBDEFLATE_SOURCES}
)

target_include_directories(dexpp_core PUBLIC src)
target_include_directories(dexpp_core PRIVATE ${libdeflate_SOURCE_DIR})
# Required for linking into the Python shared module.
set_target_properties(dexpp_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# C ABI shim: a static library (libdexpp_c.a) exposing bindings/c/dexpp.h, for
# consumers that speak C rather than C++ (e.g. Go via cgo). Bundles dexpp_core.
if(DEXPP_BUILD_C)
    add_library(dexpp_c STATIC bindings/c/dexpp_c.cpp)
    target_include_directories(dexpp_c PUBLIC bindings/c)
    target_link_libraries(dexpp_c PUBLIC dexpp_core)
    set_target_properties(dexpp_c PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(DEXPP_BUILD_TESTS)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/52eb8108c5bdec04579160ae17225d66034bd723.zip
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(main_test
        src/main.cpp
    )

    target_link_libraries(main_test PRIVATE dexpp_core)

    enable_testing()

    file(GLOB_RECURSE TEST_SOURCES
        tests/*.cc
    )

    add_executable(unit_tests
        ${TEST_SOURCES}
    )

    target_link_libraries(unit_tests
        dexpp_core
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(unit_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

if(DEXPP_BUILD_FUZZERS)
    # One executable per fuzz/fuzz_*.cc, each linking dexpp_core.  The global
    # instrumentation above already added fuzzer-no-link coverage + ASan/UBSan
    # to every object; here we add the `fuzzer` entry point (libFuzzer's main).
    file(GLOB FUZZ_SOURCES CONFIGURE_DEPENDS fuzz/fuzz_*.cc)
    foreach(fuzz_src ${FUZZ_SOURCES})
        get_filename_component(fuzz_name ${fuzz_src} NAME_WE)
        add_executable(${fuzz_name} ${fuzz_src})
        target_link_libraries(${fuzz_name} PRIVATE dexpp_core)
        target_compile_options(${fuzz_name} PRIVATE -fsanitize=fuzzer)
        target_link_options(${fuzz_name} PRIVATE -fsanitize=fuzzer)
    endforeach()
endif()

if(DEXPP_BUILD_PYTHON)
    FetchContent_Declare(
            pybind11
            URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.zip
    )
    FetchContent_MakeAvailable(pybind11)

    pybind11_add_module(dexpp bindings/python/dexpp_module.cpp)
    target_link_libraries(dexpp PRIVATE dexpp_core)

    if(SKBUILD)
        install(TARGETS dexpp DESTINATION .)
        # Both stub layouts: a sibling .pyi (pyright) and a PEP 561 stub-only
        # package (mypy).
        install(FILES bindings/python/dexpp.pyi DESTINATION .)
        install(FILES bindings/python/dexpp.pyi DESTINATION dexpp-stubs RENAME __init__.pyi)
        # libdeflate is built into the module; its MIT license ships in the
        # wheel's .dist-info/licenses directory.
        install(FILES ${libdeflate_SOURCE_DIR}/COPYING
                DESTINATION ${SKBUILD_METADATA_DIR}/licenses RENAME libdeflate-COPYING)
    endif()
endif()
