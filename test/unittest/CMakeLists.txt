

IF(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
ENDIF(COMMAND cmake_policy)

# ___________________________________________________ Expression Tree Unit Tests
#
#

ADD_EXECUTABLE (DevReg tcpcomms/DevReg.cpp)
TARGET_LINK_LIBRARIES (DevReg ${UNITTEST_LIBS} ${LIBS})
ADD_TEST(DevReg DevReg)

