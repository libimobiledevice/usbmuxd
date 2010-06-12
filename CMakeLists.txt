PROJECT(usbmuxd)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/Modules/")

include(VersionTag)

set(USBMUXD_VERSION "${VERSION_TAG}")
set(LIBUSBMUXD_VERSION "${VERSION_TAG}")
set(LIBUSBMUXD_SOVERSION "1")

message("-- Configuring usbmuxd v${VERSION_TAG}")

if(NOT DEFINED LIB_SUFFIX)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(LIB_SUFFIX "64" CACHE STRING "Define suffix of library directory name (32/64)" )
  else()
    set(LIB_SUFFIX "" CACHE STRING "Define suffix of library directory name (32/64)" )
  endif()
endif()

# let CFLAGS env override this
if(CMAKE_C_FLAGS STREQUAL "")
  set(CMAKE_C_FLAGS "-O2")
endif()

option(WANT_PLIST "Build with protocol version 1 support using libplist" ON)

set(OPT_INCLUDES "")
set(OPT_LIBS "")
if(WANT_PLIST)
  find_package(PLIST)
  if(PLIST_FOUND)
    set(HAVE_PLIST ON)
    set(OPT_INCLUDES ${OPT_INCLUDES} ${PLIST_INCLUDE_DIRS})
    set(OPT_LIBS ${OPT_LIBS} ${PLIST_LIBRARIES})
  else()
    message("* NOTE: libplist was not found!")
    message("* libusbmuxd/usbmuxd will be build WITHOUT support for version 1")
    message("* of the usbmux protocol (plist based).")
  endif()
endif()

option(WITH_USBMUXD "Build usbmux daemon (usbmuxd)" ON)
if(WIN32 AND WITH_USBMUXD)
  message("** NOTE: usbmuxd cannot be built on WIN32 due to missing libusb-1.0 support!")
  message("   If you need your own usbmuxd you have to use usbmuxd-legacy which works")
  message("   with libusb-0.1; otherwise just use the one that ships with iTunes.")
  message("   Building of usbmuxd has been disabled.")
  set(WITH_USBMUXD OFF)
endif()
if(WITH_USBMUXD)
  message("-- Will build usbmuxd: YES")
else()
  message("-- Will build usbmuxd: NO")
  message("** NOTE: will NOT build usbmuxd **")
  if(WIN32 OR APPLE)
    message("** Make sure iTunes is installed, otherwise this software will not work! **")
  else()
    message("** You'll need a working usbmuxd implementation for this software to work! **")
  endif()
endif()

add_definitions(-Wall)

add_subdirectory (libusbmuxd)
if (WITH_USBMUXD)
  add_subdirectory (daemon)
  if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_subdirectory (udev)
  endif()
endif()
add_subdirectory (tools)

# pkg-config
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libusbmuxd.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/libusbmuxd.pc")
# install pkg-config file
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libusbmuxd.pc" DESTINATION lib${LIB_SUFFIX}/pkgconfig/)

# add uninstall target
configure_file("${CMAKE_SOURCE_DIR}/Modules/cmake_uninstall.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" IMMEDIATE @ONLY)
add_custom_target(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
