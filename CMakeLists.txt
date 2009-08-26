PROJECT(usbmuxd)

SET ( USBMUXD_VERSION_MAJOR "1" )
SET ( USBMUXD_VERSION_MINOR "0" )
SET ( LIBUSBMUXD_SOVERSION "0" )
SET ( USBMUXD_VERSION "${USBMUXD_VERSION_MAJOR}.${USBMUXD_VERSION_MINOR}" )
SET ( LIBUSBMUXD_VERSION "${LIBUSBMUXD_SOVERSION}.${USBMUXD_VERSION}" )

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/Modules/")

add_subdirectory (libusbmuxd)
add_subdirectory (daemon)
add_subdirectory (tools)
add_subdirectory (udev)

# add uninstall target
CONFIGURE_FILE( "${CMAKE_SOURCE_DIR}/Modules/cmake_uninstall.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" IMMEDIATE @ONLY)
ADD_CUSTOM_TARGET(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
