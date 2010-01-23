include_directories (${CMAKE_SOURCE_DIR}/libusbmuxd)
link_directories (${CMAKE_BINARY_DIR}/libusbmuxd)

add_definitions(-O2 -g)
add_executable(iproxy iproxy.c)
target_link_libraries(iproxy libusbmuxd pthread)

install(TARGETS iproxy RUNTIME DESTINATION bin)
