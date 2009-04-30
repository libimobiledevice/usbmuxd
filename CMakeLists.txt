PROJECT(usbmuxd)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/Modules/")

find_package(USB REQUIRED)
include_directories(${USB_INCLUDE_DIRS})
set(LIBS ${LIBS} ${USB_LIBRARIES})

#set(CMAKE_VERBOSE_MAKEFILE ON)

add_definitions(-Wall -O0 -g)
add_executable(usbmuxd main.c usb-linux.c log.c utils.c device.c client.c)
target_link_libraries(usbmuxd ${LIBS})

