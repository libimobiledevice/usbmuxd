include_directories (${CMAKE_SOURCE_DIR}/common)
find_package(Threads)

add_library (libusbmuxd SHARED libusbmuxd.c sock_stuff.c ${CMAKE_SOURCE_DIR}/common/utils.c)
find_library (PTHREAD pthread)

if (HAVE_PLIST)
  message("-- libusbmuxd will be built with protocol version 1 support")
endif()
target_link_libraries (libusbmuxd ${CMAKE_THREAD_LIBS_INIT} ${OPT_LIBS})

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
