PROJECT(usbmuxd)

set(USBMUXD_VERSION "1.0.0")
set(LIBUSBMUXD_VERSION "${USBMUXD_VERSION}")
set(LIBUSBMUXD_SOVERSION "1")

cmake_minimum_required(VERSION 2.6)

if(NOT DEFINED LIB_SUFFIX)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(LIB_SUFFIX "64" CACHE STRING "Define suffix of library directory name (32/64)" )
    else(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(LIB_SUFFIX "" CACHE STRING "Define suffix of library directory name (32/64)" )
    endif(CMAKE_SIZEOF_VOID_P EQUAL 8)
endif(NOT DEFINED LIB_SUFFIX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/Modules/")

# let CFLAGS env override this
if(CMAKE_C_FLAGS STREQUAL "")
    set(CMAKE_C_FLAGS "-O2")
endif(CMAKE_C_FLAGS STREQUAL "")

option(WITH_USBMUXD "Build usbmux daemon (usbmuxd)" ON)
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
  if (NOT(WIN32 OR APPLE))
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
