CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/85-usbmuxd.rules.in ${CMAKE_CURRENT_BINARY_DIR}/85-usbmuxd.rules @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/85-usbmuxd.rules DESTINATION /lib/udev/rules.d/)
