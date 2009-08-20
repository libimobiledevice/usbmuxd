PROJECT(usbmuxd)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/Modules/")

add_subdirectory (libusbmuxd)
add_subdirectory (usbmuxd)
add_subdirectory (tools)
add_subdirectory (udev)
