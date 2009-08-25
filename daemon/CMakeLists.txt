find_package(USB REQUIRED)
include_directories(${USB_INCLUDE_DIRS})
set(LIBS ${LIBS} ${USB_LIBRARIES})

include_directories (${CMAKE_SOURCE_DIR}/common)
include_directories (${CMAKE_SOURCE_DIR}/daemon)
include_directories (${CMAKE_SOURCE_DIR}/libusbmuxd)

include(CheckConstantExists)
set(CMAKE_REQUIRED_INCLUDES ${USB_INCLUDE_DIRS})
check_constant_exists(LIBUSB_TRANSFER_ZERO_PACKET libusb.h HAVE_LIBUSB_ZLP)

if(NOT HAVE_LIBUSB_ZLP)
  message("
================================================================================
==================================== WARNING ===================================
================================================================================
Your libusb is missing proper Zero Length Packet support! If you are using a
recent libusb Git, things may or may not work. If you are using libusb 1.0.2 or
earlier, things will definitely not work properly.

Please apply the patch in the contrib/ directory to your libusb 1.0 tree.
================================================================================
")
  add_definitions(-DEXPLICIT_ZLP_TRANSACTION)
endif(NOT HAVE_LIBUSB_ZLP)

add_definitions(-Wall -O2 -g -DUSBMUXD_DAEMON)
add_executable(usbmuxd main.c usb-linux.c log.c ../common/utils.c device.c client.c)
target_link_libraries(usbmuxd ${LIBS})

install(TARGETS usbmuxd RUNTIME DESTINATION sbin)
