########################################################################
# x6losim test CMake build file.
#
# Linux: Generating Makefile
#     create build/debug build/release
#     cmake -DCMAKE_BUILD_TYPE=Release ../../
#     cmake -DCMAKE_BUILD_TYPE=Debug ../../
#
# Usage:
# 
#
########################################################################

# The name of our project is "x6losim". CMakeLists files in this project can
# refer to the root source directory of the project as ${x6losim_SOURCE_DIR} and
# to the root binary directory of the project as ${x6losim_BINARY_DIR}.
CMAKE_MINIMUM_REQUIRED (VERSION 2.8)
PROJECT (x6losim)

# The version number.
SET (x6losim_VERSION_MAJOR 4)
SET (x6losim_VERSION_MINOR 1)

SET(x6losim_PROJECT_DIR "${x6losim_SOURCE_DIR}")
SET(x6losim_SOURCE_DIR "${x6losim_PROJECT_DIR}/src")
SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

ENABLE_TESTING() # once
INCLUDE(CTest)
#INCLUDE(Dart)



# pthreads
FIND_PACKAGE(PThread REQUIRED)
INCLUDE_DIRECTORIES(${PThread_INCLUDE_DIR})
SET(LIBS ${LIBS} ${PThread_LIBRARY})

# and google test framework
FIND_PACKAGE(GTest REQUIRED)
INCLUDE_DIRECTORIES(${GTest_INCLUDE_DIR})
SET(UNITTEST_LIBS ${GTest_LIBRARY} ${GTestMain_LIBRARY})
SET(SYSTEST_LIBS ${GTest_LIBRARY} ${GTestMain_LIBRARY})

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall")
SET(CMAKE_DEBUG_POSTFIX "d")

# For debug builds turn on pedantic, for release treat errors as warnings.
IF(CMAKE_BUILD_TYPE STREQUAL "Debug")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic")
    ADD_DEFINITIONS(-DDEBUG)
ELSE(CMAKE_BUILD_TYPE STREQUAL "Debug")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror -Wextra")
ENDIF(CMAKE_BUILD_TYPE STREQUAL "Debug")


INCLUDE_DIRECTORIES(${x6losim_SOURCE_DIR})

#SET(INTERFACE_FILE "${x6losim_PROJECT_DIR}/snmp/sscd_x6losim_interface.h")

# Create an application called "x6losim"
ADD_EXECUTABLE (x6losim
                ${x6losim_SOURCE_DIR}/main.cpp
                ${x6losim_SOURCE_DIR}/log/log.cpp
                ${x6losim_SOURCE_DIR}/sim/DeviceNode.cpp
                ${x6losim_SOURCE_DIR}/sim/PhysicalMedium.cpp
                ${x6losim_SOURCE_DIR}/sim/Simulator.cpp
                ${x6losim_SOURCE_DIR}/socket/Socket.cpp
                ${x6losim_SOURCE_DIR}/utils/time.cpp
)


# Specify external libraries to link to.
TARGET_LINK_LIBRARIES(x6losim ${PThread_LIBRARY} m rt ${LIBS} )

# Enable Test Directory
#IF(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "debug")
#    ADD_SUBDIRECTORY(test)
#ENDIF (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "debug")
