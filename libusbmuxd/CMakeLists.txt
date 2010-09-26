include_directories (${CMAKE_SOURCE_DIR}/common)
find_package(Threads)

add_library (libusbmuxd SHARED libusbmuxd.c sock_stuff.c ${CMAKE_SOURCE_DIR}/common/utils.c)
find_library (PTHREAD pthread)

if (HAVE_PLIST)
  add_definitions("-DHAVE_PLIST")
  message("-- libusbmuxd will be built with protocol version 1 support")
endif()
if(WIN32)
  set(OPT_LIBS ${OPT_LIBS} ws2_32)
endif()
include_directories(${OPT_INCLUDES})
target_link_libraries (libusbmuxd ${CMAKE_THREAD_LIBS_INIT} ${OPT_LIBS})

# 'lib' is a UNIXism, the proper CMake target is usbmuxd
# But we can't use that due to the conflict with the usbmuxd daemon,
# so instead change the library output base name to usbmuxd here
set_target_properties(libusbmuxd PROPERTIES OUTPUT_NAME usbmuxd)
set_target_properties(libusbmuxd PROPERTIES VERSION ${LIBUSBMUXD_VERSION})
set_target_properties(libusbmuxd PROPERTIES SOVERSION ${LIBUSBMUXD_SOVERSION})

if(APPLE)
  set_target_properties(libusbmuxd PROPERTIES INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}")
endif()

install(TARGETS libusbmuxd
	RUNTIME DESTINATION bin
	ARCHIVE DESTINATION lib${LIB_SUFFIX}
	LIBRARY DESTINATION lib${LIB_SUFFIX}
)
install(FILES usbmuxd.h usbmuxd-proto.h DESTINATION include)
