include_directories (${CMAKE_SOURCE_DIR}/libusbmuxd)
link_directories (${CMAKE_BINARY_DIR}/libusbmuxd)

add_executable(iproxy iproxy.c)
if(WIN32)
  target_link_libraries(iproxy libusbmuxd pthread ws2_32)
else()
  target_link_libraries(iproxy libusbmuxd pthread)
endif()

install(TARGETS iproxy RUNTIME DESTINATION bin)
