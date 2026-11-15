cmake_minimum_required(VERSION 3.18)

# The version lives in src/version.hpp (shared with the Python package metadata).
file(STRINGS src/version.hpp DEXPP_VERSION_LINE REGEX "^#define DEXPP_VERSION \"")
string(REGEX REPLACE "^#define DEXPP_VERSION \"([^\"]+)\".*" "\\1" DEXPP_VERSION "${DEXPP_VERSION_LINE}")
project(dex++ VERSION ${DEXPP_VERSION})

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DEXPP_BUILD_PYTHON "Build the dexpp Python extension module" OFF)
option(DEXPP_BUILD_TESTS "Build the unit tests and demo CLI" ON)
option(DEXPP_BUILD_C "Build the C ABI shim (bindings/c, for Go/cgo and other FFI)" ON)
set(DEXPP_SANITIZE "" CACHE STRING
    "Sanitizers to build everything with, e.g. 'address,undefined' (empty = off)")
option(DEXPP_WERROR "Treat compiler warnings in dex++'s own sources as errors" OFF)

# SKBUILD is set by scikit-build-core during `pip install` / wheel builds:
# build only the Python module there, no tests, demo CLI, or C shim.
if(SKBUILD)
    set(DEXPP_BUILD_PYTHON ON)
    set(DEXPP_BUILD_TESTS OFF)
    set(DEXPP_BUILD_C OFF)
endif()

# Sanitizer instrumentation is applied directory-wide (before any target is
# declared) so the library and the fetched test frameworks are built
# consistently.  The flags are GCC / Clang ones.
if(DEXPP_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "DEXPP_SANITIZE needs GCC or Clang; it is not supported with MSVC.")
    endif()
    add_compile_options(-fsanitize=${DEXPP_SANITIZE}
                        -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${DEXPP_SANITIZE})
endif()

if(MSVC)
    # For every target, fetched ones included:
    #   /utf-8          the sources are UTF-8 (comments hold non-ASCII text)
    #   /permissive-    standard C++ rules, like GCC and Clang
    #   /Zc:__cplusplus __cplusplus reports the real language version
    #   /bigobj         the std::variant-heavy files exceed the default section limit
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/permissive-;/Zc:__cplusplus;/bigobj>")
endif()

# Warnings are turned on for dex++'s own sources only.  The fetched libdeflate,
# googletest and pybind11 sources are not ours to fix, so they keep their own
# flags.  -Werror is opt-in (CI sets it): a compiler newer than the ones CI
# uses may warn about something new, and that must not break a user's build.
if(MSVC)
    # /external:*: headers included with <...> (googletest, pybind11, the
    # standard library) are not ours, so their warnings are not reported.
    set(DEXPP_WARNING_FLAGS /W4 /external:anglebrackets /external:W0)
    if(DEXPP_WERROR)
        list(APPEND DEXPP_WARNING_FLAGS /WX)
    endif()
else()
    set(DEXPP_WARNING_FLAGS -Wall -Wextra)
    if(DEXPP_WERROR)
        list(APPEND DEXPP_WARNING_FLAGS -Werror)
    endif()
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

set(DEXPP_CORE_SOURCES
    src/common/ids.cpp
    src/raw/error.cpp
    src/raw/parser.cpp
    src/raw/types.cpp
    src/raw/parsing_utils.cpp
    src/raw/zip.cpp
    src/raw/file_io.cpp
    src/raw/mutf8.cpp
    src/raw/descriptors.cpp
    src/raw/digest.cpp
    src/raw/opcodes.cpp
    src/model/types.cpp
    src/model/read.cpp
    src/model/write.cpp
    src/model/validate.cpp
    src/model/rename.cpp
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
)
# Per source file, not per target: dexpp_core also holds the libdeflate sources.
set_source_files_properties(${DEXPP_CORE_SOURCES} PROPERTIES COMPILE_OPTIONS "${DEXPP_WARNING_FLAGS}")
# file_io.cpp uses fopen and strerror, which are standard C.  MSVC calls them
# unsafe and asks for its own fopen_s / strerror_s instead; this file keeps the
# portable functions.
if(MSVC)
    set_source_files_properties(src/raw/file_io.cpp PROPERTIES COMPILE_DEFINITIONS
                                _CRT_SECURE_NO_WARNINGS)
endif()

add_library(dexpp_core STATIC
    ${DEXPP_CORE_SOURCES}
    ${LIBDEFLATE_SOURCES}
)

target_include_directories(dexpp_core PUBLIC src)
target_include_directories(dexpp_core PRIVATE ${libdeflate_SOURCE_DIR})
# libdeflate is C11 (its C files are the only C sources in the project).
set_target_properties(dexpp_core PROPERTIES C_STANDARD 11 C_EXTENSIONS OFF)
# Required for linking into the Python shared module.
set_target_properties(dexpp_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# C ABI shim: a static library (libdexpp_c.a) exposing bindings/c/dexpp.h, for
# consumers that speak C rather than C++ (e.g. Go via cgo). Bundles dexpp_core.
if(DEXPP_BUILD_C)
    add_library(dexpp_c STATIC bindings/c/dexpp_c.cpp bindings/c/dexpp_c_code.cpp
                               bindings/c/dexpp_c_graph.cpp bindings/c/dexpp_c_apk.cpp
                               bindings/c/dexpp_model.cpp)
    target_include_directories(dexpp_c PUBLIC bindings/c)
    target_link_libraries(dexpp_c PUBLIC dexpp_core)
    target_compile_options(dexpp_c PRIVATE ${DEXPP_WARNING_FLAGS})
    set_target_properties(dexpp_c PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(DEXPP_BUILD_TESTS)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/52eb8108c5bdec04579160ae17225d66034bd723.zip
    )
    # With MSVC, build googletest against the same C runtime (/MD) as the rest
    # of the project; its default (/MT) does not link with it.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(main_test
        src/main.cpp
    )

    target_link_libraries(main_test PRIVATE dexpp_core)
    target_compile_options(main_test PRIVATE ${DEXPP_WARNING_FLAGS})

    enable_testing()

    file(GLOB_RECURSE TEST_SOURCES
        tests/*.cc
    )
    # tests/c/ calls the C ABI shim, so it is built only when the shim is.
    if(NOT DEXPP_BUILD_C)
        list(FILTER TEST_SOURCES EXCLUDE REGEX "/tests/c/")
    endif()

    add_executable(unit_tests
        ${TEST_SOURCES}
    )

    target_link_libraries(unit_tests
        dexpp_core
        GTest::gtest_main
    )
    if(DEXPP_BUILD_C)
        target_link_libraries(unit_tests dexpp_c)
    endif()
    target_compile_options(unit_tests PRIVATE ${DEXPP_WARNING_FLAGS})

    include(GoogleTest)
    gtest_discover_tests(unit_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    # The C++ examples the documentation shows (docs/examples/cpp).  They are
    # built and run with the tests, so the documentation cannot show code that
    # no longer compiles or no longer works.
    file(GLOB DOC_EXAMPLE_SOURCES docs/examples/cpp/*.cpp)
    foreach(example_source ${DOC_EXAMPLE_SOURCES})
        get_filename_component(example_name ${example_source} NAME_WE)
        add_executable(example_${example_name} ${example_source})
        target_link_libraries(example_${example_name} PRIVATE dexpp_core)
        target_compile_options(example_${example_name} PRIVATE ${DEXPP_WARNING_FLAGS})
        add_test(NAME example_${example_name} COMMAND example_${example_name}
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endforeach()
endif()

if(DEXPP_BUILD_PYTHON)
    FetchContent_Declare(
            pybind11
            URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.zip
    )
    FetchContent_MakeAvailable(pybind11)

    pybind11_add_module(dexpp bindings/python/dexpp_module.cpp bindings/python/dexpp_model_py.cpp)
    target_link_libraries(dexpp PRIVATE dexpp_core)
    target_compile_options(dexpp PRIVATE ${DEXPP_WARNING_FLAGS})
    # Put the module at the top of the build directory with every generator.
    # Multi-config generators (Visual Studio, Xcode) would otherwise use
    # build/Release/, and `PYTHONPATH=build` would not find it.
    set_target_properties(dexpp PROPERTIES LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}>")

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
