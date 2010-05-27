# - Try to find libplist
# Once done, this will define
#
#  PLIST_FOUND - system has libplist
#  PLIST_INCLUDE_DIRS - the libplist include directories
#  PLIST_LIBRARIES - link these to use libplist

include(LibFindMacros)

# Dependencies

# Use pkg-config to get hints about paths
libfind_pkg_check_modules(PLIST_PKGCONF libplist >= 0.15)

# Include dir
find_path(PLIST_INCLUDE_DIR
  NAMES plist/plist.h
  PATHS ${PLIST_PKGCONF_INCLUDE_DIRS}
)

# Finally the library itself
find_library(PLIST_LIBRARY
  NAMES plist
  PATHS ${PLIST_PKGCONF_LIBRARY_DIRS}
)

# Set the include dir variables and the libraries and let libfind_process do the rest.
# NOTE: Singular variables for this library, plural for libraries that this lib depends on.
set(PLIST_PROCESS_INCLUDES PLIST_INCLUDE_DIR)
set(PLIST_PROCESS_LIBS PLIST_LIBRARY)
libfind_process(PLIST)
