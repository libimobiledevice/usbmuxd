PROJECT(usbmuxd)

SET ( USBMUXD_VERSION "1.0.0" )
SET ( LIBUSBMUXD_VERSION "${USBMUXD_VERSION}" )
SET ( LIBUSBMUXD_SOVERSION "1" )

cmake_minimum_required(VERSION 2.6)

IF(NOT DEFINED LIB_SUFFIX)
    IF(CMAKE_SIZEOF_VOID_P EQUAL 8)
        SET(LIB_SUFFIX "64" CACHE STRING "Define suffix of library directory name (32/64)" )
    ELSE(CMAKE_SIZEOF_VOID_P EQUAL 8)
        SET(LIB_SUFFIX "" CACHE STRING "Define suffix of library directory name (32/64)" )
    ENDIF(CMAKE_SIZEOF_VOID_P EQUAL 8)
ENDIF(NOT DEFINED LIB_SUFFIX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/Modules/")

add_definitions(-Wall)

add_subdirectory (libusbmuxd)
add_subdirectory (daemon)
add_subdirectory (tools)
add_subdirectory (udev)

# pkg-config
CONFIGURE_FILE ("${CMAKE_SOURCE_DIR}/libusbmuxd.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/libusbmuxd.pc")
# install pkg-config file
INSTALL (FILES "${CMAKE_CURRENT_BINARY_DIR}/libusbmuxd.pc" DESTINATION lib${LIB_SUFFIX}/pkgconfig/)

# add uninstall target
CONFIGURE_FILE( "${CMAKE_SOURCE_DIR}/Modules/cmake_uninstall.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" IMMEDIATE @ONLY)
ADD_CUSTOM_TARGET(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
