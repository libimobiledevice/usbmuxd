find_package(USB REQUIRED)
include_directories(${USB_INCLUDE_DIRS})
set(LIBS ${LIBS} ${USB_LIBRARIES})

add_definitions(-Wall -O2 -g)
add_executable(usbmuxd main.c usb-linux.c log.c utils.c device.c client.c)
target_link_libraries(usbmuxd ${LIBS})

install(TARGETS usbmuxd RUNTIME DESTINATION sbin)