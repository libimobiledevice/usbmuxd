PROJECT(usbmuxd)

set(USBMUXD_VERSION "1.0.0")
set(LIBUSBMUXD_VERSION "${USBMUXD_VERSION}")
set(LIBUSBMUXD_SOVERSION "1")

cmake_minimum_required(VERSION 2.6)

if(NOT DEFINED LIB_SUFFIX)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(LIB_SUFFIX "64" CACHE STRING "Define suffix of library directory name (32/64)" )
    else(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(LIB_SUFFIX "" CACHE STRING "Define suffix of library directory name (32/64)" )
    endif(CMAKE_SIZEOF_VOID_P EQUAL 8)
endif(NOT DEFINED LIB_SUFFIX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/Modules/")

# let CFLAGS env override this
if(CMAKE_C_FLAGS STREQUAL "")
    set(CMAKE_C_FLAGS "-O2")
endif(CMAKE_C_FLAGS STREQUAL "")

add_definitions(-Wall)

add_subdirectory (libusbmuxd)
add_subdirectory (daemon)
add_subdirectory (tools)
add_subdirectory (udev)

# pkg-config
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libusbmuxd.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/libusbmuxd.pc")
# install pkg-config file
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libusbmuxd.pc" DESTINATION lib${LIB_SUFFIX}/pkgconfig/)

# add uninstall target
configure_file("${CMAKE_SOURCE_DIR}/Modules/cmake_uninstall.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" IMMEDIATE @ONLY)
add_custom_target(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
