set(INOTIFY_H "NOTFOUND")
find_file(INOTIFY_H
  "sys/inotify.h"
  PATHS ENV INCLUDE
)

if (INOTIFY_H)
  set(INOTIFY_FOUND TRUE)
else()
  set(INOTIFY_FOUND FALSE)
endif()
