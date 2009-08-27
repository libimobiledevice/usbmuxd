PROJECT(usbmuxd)

SET ( USBMUXD_VERSION "1.0.0" )
SET ( LIBUSBMUXD_VERSION "${USBMUXD_VERSION}" )
SET ( LIBUSBMUXD_SOVERSION "1" )

cmake_minimum_required(VERSION 2.6)

IF(NOT DEFINED CMAKE_INSTALL_LIBDIR)
    SET(CMAKE_INSTALL_LIBDIR lib CACHE PATH "Output directory for libraries")
ENDIF(NOT DEFINED CMAKE_INSTALL_LIBDIR)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/Modules/")

add_subdirectory (libusbmuxd)
add_subdirectory (daemon)
add_subdirectory (tools)
add_subdirectory (udev)

# pkg-config
CONFIGURE_FILE ("${CMAKE_SOURCE_DIR}/libusbmuxd.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/libusbmuxd.pc")
# install pkg-config file
INSTALL (FILES "${CMAKE_CURRENT_BINARY_DIR}/libusbmuxd.pc" DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig/)

# add uninstall target
CONFIGURE_FILE( "${CMAKE_SOURCE_DIR}/Modules/cmake_uninstall.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" IMMEDIATE @ONLY)
ADD_CUSTOM_TARGET(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
