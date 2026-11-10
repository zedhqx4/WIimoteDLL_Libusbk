cmake_minimum_required(VERSION 3.20)
project(WiiMoteDLL_Libusbk VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Reuse a libusb target supplied by the parent application when embedded through
# FetchContent; otherwise locate an installed libusb development package.
if(TARGET usb-1.0)
  set(WII_BT_LIBUSB_TARGET usb-1.0)
else()
  find_path(LIBUSB_INCLUDE_DIR NAMES libusb.h PATH_SUFFIXES libusb-1.0 REQUIRED)
  find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0 REQUIRED)
  add_library(wii_bt_libusb UNKNOWN IMPORTED)
  set_target_properties(wii_bt_libusb PROPERTIES
    IMPORTED_LOCATION "${LIBUSB_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB_INCLUDE_DIR}")
  set(WII_BT_LIBUSB_TARGET wii_bt_libusb)
endif()

if(WIN32)
  add_library(wii_bt_passthrough SHARED src/wii_bt_passthrough.cpp src/wii_bt_reports.cpp)
  target_include_directories(wii_bt_passthrough
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(wii_bt_passthrough PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX WII_BT_DLL_BUILD HOTD2_WII_USB_DLL_BUILD)
  target_link_libraries(wii_bt_passthrough PRIVATE ${WII_BT_LIBUSB_TARGET} winmm)

  # When this is embedded by HOTD2RE's FetchContent build, libusb is a
  # shared target. Keep its runtime beside the optional DLL for Windows loader
  # dependency resolution. Standalone vcpkg builds stage libusb separately.
  if(TARGET usb-1.0)
    add_custom_command(TARGET wii_bt_passthrough POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:usb-1.0>" "$<TARGET_FILE_DIR:wii_bt_passthrough>"
      VERBATIM)
  endif()
  set_target_properties(wii_bt_passthrough PROPERTIES
    OUTPUT_NAME "wii_bt_passthrough"
    WINDOWS_EXPORT_ALL_SYMBOLS OFF)
endif()

option(WII_BT_BUILD_DIAGNOSTIC "Build the hardware discovery/pairing diagnostic" ON)
if(WII_BT_BUILD_DIAGNOSTIC)
  add_executable(wii_discovery src/wii_discovery.cpp)
  target_link_libraries(wii_discovery PRIVATE ${WII_BT_LIBUSB_TARGET})
endif()

if(WIN32)
  add_executable(wii_bt_api_smoke tests/wii_bt_api_smoke.cpp)
  target_include_directories(wii_bt_api_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(wii_bt_api_smoke PRIVATE wii_bt_passthrough)
  enable_testing()
  add_test(NAME wii_bt_api_smoke COMMAND wii_bt_api_smoke)
  add_executable(wii_bt_reports_test tests/wii_bt_reports_test.cpp src/wii_bt_reports.cpp)
  target_include_directories(wii_bt_reports_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME wii_bt_reports_test COMMAND wii_bt_reports_test)
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
