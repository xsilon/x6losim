
# Ensure we add the test directory to the include path
INCLUDE_DIRECTORIES(..)

# Create a library of the SSMD base code so we can link system tests to it.
ADD_LIBRARY(
	x6losim-lib 
	${x6losim_SOURCE_DIR}/main.cpp
	${x6losim_SOURCE_DIR}/log/log.cpp
	${x6losim_SOURCE_DIR}/sim/DeviceNode.cpp
	${x6losim_SOURCE_DIR}/sim/PhysicalMedium.cpp
	${x6losim_SOURCE_DIR}/sim/Simulator.cpp
	${x6losim_SOURCE_DIR}/socket/Socket.cpp
	${x6losim_SOURCE_DIR}/utils/time.cpp
)

IF(COMMAND cmake_policy)
	cmake_policy(SET CMP0003 NEW)
ENDIF(COMMAND cmake_policy)


# ______________________________________________________________________________
# 
# Here we define the System tests
#
# ========

SET(LIBS ${PThread_LIBRARY})

#SET_PROPERTY(
#	SOURCE DevReg.cc
#	APPEND PROPERTY COMPILE_DEFINITIONS TEST_FILENAME="systest-cfg-load-delta")
ADD_EXECUTABLE (DevReg systemtest.cpp DevReg.cpp)
TARGET_LINK_LIBRARIES (DevReg  x6losim-lib ${LIBS} ${SYSTEST_LIBS})
ADD_TEST(DevReg DevReg)


