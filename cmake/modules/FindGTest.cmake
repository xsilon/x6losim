# - Try to find GTest
# Once done, this will define
#
#  GTest_FOUND - system has GTest
#  GTest_INCLUDE_DIRS - the GTest include directories
#  GTest_LIBRARIES - link these to use GTest


FIND_PATH(GTest_INCLUDE_DIR gtest.h /usr/include/gtest /usr/local/include/gtest )

FIND_LIBRARY(GTest_LIBRARY gtest /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64)
FIND_LIBRARY(GTestMain_LIBRARY gtest_main /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64)

IF (GTest_INCLUDE_DIR AND GTest_LIBRARY AND GTestMain_LIBRARY)
   SET(GTest_FOUND TRUE)
ENDIF (GTest_INCLUDE_DIR AND GTest_LIBRARY AND GTestMain_LIBRARY)


IF (GTest_FOUND)
   IF (NOT GTest_FIND_QUIETLY)
      MESSAGE(STATUS "Found GTest: ${GTest_LIBRARY}")
   ENDIF (NOT GTest_FIND_QUIETLY)
ELSE (GTest_FOUND)
   IF (GTest_FIND_REQUIRED)
      MESSAGE(FATAL_ERROR "Could not find GTest")
      MESSAGE(FATAL_ERROR "Include: ${GTest_INCLUDE_DIR}")
      MESSAGE(FATAL_ERROR "Library: ${GTest_LIBRARY}")
      MESSAGE(FATAL_ERROR "Main Library: ${GTestMain_LIBRARY}")
   ENDIF (GTest_FIND_REQUIRED)
ENDIF (GTest_FOUND)

MARK_AS_ADVANCED(GTest_INCLUDE_DIR GTest_LIBRARY)
    