# - Try to find libpcap
# Once done, this will define
#
#  PCap_FOUND - system has pthreads
#  PCap_INCLUDE_DIRS - the pthreads include directories
#  PCap_LIBRARIES - link these to use pthreads


FIND_PATH(PCap_INCLUDE_DIR pcap.h /usr/include/)

FIND_LIBRARY(PCap_LIBRARY pcap /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib/i386-linux-gnu)

IF (PCap_INCLUDE_DIR AND PCap_LIBRARY)
   SET(PCap_FOUND TRUE)
ENDIF (PCap_INCLUDE_DIR AND PCap_LIBRARY)


IF (PCap_FOUND)
   IF (NOT PCap_FIND_QUIETLY)
      MESSAGE(STATUS "Found PCap: ${PCap_LIBRARY}")
   ENDIF (NOT PCap_FIND_QUIETLY)
ELSE (PCap_FOUND)
   IF (PCap_FIND_REQUIRED)
      MESSAGE(FATAL_ERROR "Could not find PCap")
   ENDIF (PCap_FIND_REQUIRED)
ENDIF (PCap_FOUND)

MARK_AS_ADVANCED(PCap_INCLUDE_DIR PCap_LIBRARY)
