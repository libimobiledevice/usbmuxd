# - Try to find USB
# Once done this will define
#
#  USB_FOUND - system has USB
#  USB_INCLUDE_DIRS - the USB include directory
#  USB_LIBRARIES - Link these to use USB
#  USB_DEFINITIONS - Compiler switches required for using USB
#
#  Copyright (c) 2006 Andreas Schneider <mail@cynapses.org>
#  Copyright (c) 2008 Andreas Schneider <mail@cynapses.org>
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#  For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#


if (USB_LIBRARIES AND USB_INCLUDE_DIRS)
  # in cache already
  set(USB_FOUND TRUE)
else (USB_LIBRARIES AND USB_INCLUDE_DIRS)
  find_path(USB_INCLUDE_DIR
    NAMES
      usb.h
    PATHS
      /usr/include
      /usr/local/include
      /opt/local/include
      /sw/include
  )

  find_library(USB_LIBRARY
    NAMES
      usb
    PATHS
      /usr/lib
      /usr/local/lib
      /opt/local/lib
      /sw/lib
  )

  set(USB_INCLUDE_DIRS
    ${USB_INCLUDE_DIR}
  )
  set(USB_LIBRARIES
    ${USB_LIBRARY}
)

  if (USB_INCLUDE_DIRS AND USB_LIBRARIES)
     set(USB_FOUND TRUE)
  endif (USB_INCLUDE_DIRS AND USB_LIBRARIES)

  if (USB_FOUND)
    if (NOT USB_FIND_QUIETLY)
      message(STATUS "Found USB: ${USB_LIBRARIES}")
    endif (NOT USB_FIND_QUIETLY)
  else (USB_FOUND)
    if (USB_FIND_REQUIRED)
      message(FATAL_ERROR "Could not find USB")
    endif (USB_FIND_REQUIRED)
  endif (USB_FOUND)

  # show the USB_INCLUDE_DIRS and USB_LIBRARIES variables only in the advanced view
  mark_as_advanced(USB_INCLUDE_DIRS USB_LIBRARIES)

endif (USB_LIBRARIES AND USB_INCLUDE_DIRS)


