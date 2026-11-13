cmake_minimum_required(VERSION 3.21)

project(WiimoteBackend LANGUAGES CXX)

add_library(wiimote_core STATIC
    src/protocol.cpp
    src/math.cpp
)
add_library(wiimote::core ALIAS wiimote_core)

target_compile_features(wiimote_core PUBLIC cxx_std_20)
target_include_directories(wiimote_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(_wiimote_tests_default ON)
else()
    set(_wiimote_tests_default OFF)
endif()
option(WIIMOTE_BUILD_TESTS "Build the standalone protocol and math tests" ${_wiimote_tests_default})

if(WIIMOTE_BUILD_TESTS)
    enable_testing()

    add_executable(wiimote_test_protocol tests/test_protocol.cpp)
    target_link_libraries(wiimote_test_protocol PRIVATE wiimote::core)
    add_test(NAME wiimote.protocol COMMAND wiimote_test_protocol)

    add_executable(wiimote_test_math tests/test_math.cpp)
    target_link_libraries(wiimote_test_math PRIVATE wiimote::core)
    add_test(NAME wiimote.math COMMAND wiimote_test_math)
endif()
