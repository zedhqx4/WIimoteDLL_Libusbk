cmake_minimum_required(VERSION 3.20)
project(WiiMoteDLL_Libusbk VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_path(LIBUSB_INCLUDE_DIR NAMES libusb.h PATH_SUFFIXES libusb-1.0 REQUIRED)
find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0 REQUIRED)

if(WIN32)
  add_library(wii_bt_passthrough SHARED src/wii_bt_passthrough.cpp)
  target_include_directories(wii_bt_passthrough
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src ${LIBUSB_INCLUDE_DIR})
  target_compile_definitions(wii_bt_passthrough PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX WII_BT_DLL_BUILD HOTD2_WII_USB_DLL_BUILD)
  target_link_libraries(wii_bt_passthrough PRIVATE ${LIBUSB_LIBRARY} winmm)
  set_target_properties(wii_bt_passthrough PROPERTIES
    OUTPUT_NAME "wii_bt_passthrough"
    WINDOWS_EXPORT_ALL_SYMBOLS OFF)
endif()

option(WII_BT_BUILD_DIAGNOSTIC "Build the hardware discovery/pairing diagnostic" ON)
if(WII_BT_BUILD_DIAGNOSTIC)
  add_executable(wii_discovery src/wii_discovery.cpp)
  target_include_directories(wii_discovery PRIVATE ${LIBUSB_INCLUDE_DIR})
  target_link_libraries(wii_discovery PRIVATE ${LIBUSB_LIBRARY})
endif()

if(MSVC)
  if(TARGET wii_bt_passthrough)
    target_compile_options(wii_bt_passthrough PRIVATE /W4 /permissive-)
  endif()
  if(TARGET wii_discovery)
    target_compile_options(wii_discovery PRIVATE /W4 /permissive-)
  endif()
else()
  if(TARGET wii_bt_passthrough)
    target_compile_options(wii_bt_passthrough PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  if(TARGET wii_discovery)
    target_compile_options(wii_discovery PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()
