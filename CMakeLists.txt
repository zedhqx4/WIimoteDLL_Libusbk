cmake_minimum_required(VERSION 3.20)
project(WIimoteDLL_Libusbk VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WII_BT_BUILD_HOST "Build the reusable USB Bluetooth HCI host DLL" ON)
option(WII_BT_BUILD_TOOLS "Build the standalone discovery/sample client" ON)
option(WII_BT_BUILD_TESTS "Build the host API smoke test" ON)

if(NOT WIN32 AND WII_BT_BUILD_HOST)
    message(FATAL_ERROR "The current USB Bluetooth host backend is supported on Windows only.")
endif()

if(WIN32 AND WII_BT_BUILD_HOST)
    # Reuse a libusb target provided by a parent project (for example HOTD2RE),
    # otherwise fetch the upstream libusb CMake project for a standalone build.
    if(NOT TARGET usb-1.0)
        include(FetchContent)
        set(LIBUSB_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
        set(LIBUSB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(LIBUSB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(LIBUSB_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
        set(LIBUSB_INSTALL_PKGCONFIG OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(libusb_cmake
            GIT_REPOSITORY https://github.com/libusb/libusb-cmake.git
            GIT_TAG v1.0.30-0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(libusb_cmake)
    endif()

    add_library(wii_bt_passthrough SHARED
        src/wii_remote_core.cpp
        src/wii_bt_host.cpp)
    target_include_directories(wii_bt_passthrough
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(wii_bt_passthrough PRIVATE
        WII_BT_BUILD_DLL WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(wii_bt_passthrough PRIVATE usb-1.0 winmm)
    set_target_properties(wii_bt_passthrough PROPERTIES
        OUTPUT_NAME "wii_bt_passthrough"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)

    # The DLL remains optional to applications. Its LGPL libusb runtime is
    # copied beside it so applications do not need a system-wide libusb install.
    add_custom_command(TARGET wii_bt_passthrough POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:usb-1.0>" "$<TARGET_FILE_DIR:wii_bt_passthrough>"
        VERBATIM)

    if(WII_BT_BUILD_TOOLS)
        add_executable(wii_discovery src/wii_discovery.cpp)
        target_include_directories(wii_discovery PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(wii_discovery PRIVATE wii_bt_passthrough)
        add_dependencies(wii_discovery wii_bt_passthrough)
    endif()

    if(WII_BT_BUILD_TESTS)
        enable_testing()
        add_executable(wii_bt_api_test tests/wii_bt_api_test.cpp)
        target_include_directories(wii_bt_api_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(wii_bt_api_test PRIVATE wii_bt_passthrough)
        add_test(NAME wii_bt_api_test COMMAND wii_bt_api_test)
    endif()

    if(MSVC)
        target_compile_options(wii_bt_passthrough PRIVATE /W4 /permissive-)
        if(TARGET wii_discovery)
            target_compile_options(wii_discovery PRIVATE /W4 /permissive-)
        endif()
        if(TARGET wii_bt_api_test)
            target_compile_options(wii_bt_api_test PRIVATE /W4 /permissive-)
        endif()
    else()
        target_compile_options(wii_bt_passthrough PRIVATE -Wall -Wextra -Wpedantic)
        if(TARGET wii_discovery)
            target_compile_options(wii_discovery PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        if(TARGET wii_bt_api_test)
            target_compile_options(wii_bt_api_test PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endif()
