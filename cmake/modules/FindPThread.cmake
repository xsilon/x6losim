# - Try to find PThread
# Once done, this will define
#
#  PThread_FOUND - system has pthreads
#  PThread_INCLUDE_DIRS - the pthreads include directories
#  PThread_LIBRARIES - link these to use pthreads


FIND_PATH(PThread_INCLUDE_DIR pthread.h /usr/include/)

FIND_LIBRARY(PThread_LIBRARY pthread /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib/i386-linux-gnu)

IF (PThread_INCLUDE_DIR AND PThread_LIBRARY)
   SET(PThread_FOUND TRUE)
ENDIF (PThread_INCLUDE_DIR AND PThread_LIBRARY)


IF (PThread_FOUND)
   IF (NOT PThread_FIND_QUIETLY)
      MESSAGE(STATUS "Found PThread: ${PThread_LIBRARY}")
   ENDIF (NOT PThread_FIND_QUIETLY)
ELSE (PThread_FOUND)
   IF (PThread_FIND_REQUIRED)
      MESSAGE(FATAL_ERROR "Could not find PThread")
   ENDIF (PThread_FIND_REQUIRED)
ENDIF (PThread_FOUND)

MARK_AS_ADVANCED(PThread_INCLUDE_DIR PThread_LIBRARY)
