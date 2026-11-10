cmake_minimum_required(VERSION 3.20)
project(WiiMoteDLL_Libusbk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_path(LIBUSB_INCLUDE_DIR NAMES libusb.h PATH_SUFFIXES libusb-1.0 REQUIRED)
find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0 REQUIRED)

add_executable(wii_discovery
    src/wii_discovery.cpp
)
target_include_directories(wii_discovery PRIVATE ${LIBUSB_INCLUDE_DIR})
target_link_libraries(wii_discovery PRIVATE ${LIBUSB_LIBRARY})

if(MSVC)
    target_compile_options(wii_discovery PRIVATE /W4 /permissive-)
else()
    target_compile_options(wii_discovery PRIVATE -Wall -Wextra -Wpedantic)
endif()
