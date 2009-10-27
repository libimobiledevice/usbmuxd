include_directories (${CMAKE_SOURCE_DIR}/common)

add_library (libusbmuxd SHARED libusbmuxd.c sock_stuff.c ../common/utils.c)

# 'lib' is a UNIXism, the proper CMake target is usbmuxd
# But we can't use that due to the conflict with the usbmuxd daemon,
# so instead change the library output base name to usbmuxd here
set_target_properties(libusbmuxd PROPERTIES OUTPUT_NAME usbmuxd)
set_target_properties(libusbmuxd PROPERTIES VERSION ${LIBUSBMUXD_VERSION})
set_target_properties(libusbmuxd PROPERTIES SOVERSION ${LIBUSBMUXD_SOVERSION})

install(TARGETS libusbmuxd
	ARCHIVE DESTINATION lib${LIB_SUFFIX}
	LIBRARY DESTINATION lib${LIB_SUFFIX}
)
install(FILES usbmuxd.h usbmuxd-proto.h DESTINATION include)
