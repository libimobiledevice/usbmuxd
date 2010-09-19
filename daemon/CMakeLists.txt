find_package(USB REQUIRED)
include_directories(${USB_INCLUDE_DIRS})
include_directories(${OPT_INCLUDES})
set(LIBS ${LIBS} ${USB_LIBRARIES} ${OPT_LIBS})
if(HAVE_PLIST)
  add_definitions("-DHAVE_PLIST")
  message("-- usbmuxd will be built with protocol version 1 support")
endif()
include_directories (${CMAKE_SOURCE_DIR}/common)
include_directories (${CMAKE_SOURCE_DIR}/daemon)
include_directories (${CMAKE_SOURCE_DIR}/libusbmuxd)

add_definitions(-DUSBMUXD_DAEMON -DUSBMUXD_VERSION="${USBMUXD_VERSION}")
add_executable(usbmuxd main.c usb-linux.c log.c ${CMAKE_SOURCE_DIR}/common/utils.c device.c client.c)
target_link_libraries(usbmuxd ${LIBS})

install(TARGETS usbmuxd RUNTIME DESTINATION sbin)

message("
* REMINDER
* Remember to add a user named 'usbmux' with USB access permissions
* for the udev hotplugging feature to work out of the box.
")
