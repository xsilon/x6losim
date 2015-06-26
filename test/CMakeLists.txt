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

IF(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
ENDIF(COMMAND cmake_policy)

# ___________________________________________________ Expression Tree Unit Tests
#
#

ADD_EXECUTABLE (connect_no_reg_con connect_no_reg_con.c)
TARGET_LINK_LIBRARIES (connect_no_reg_con ${LIBS} ${UNITTEST_LIBS})
ADD_TEST(connect_no_reg_con connect_no_reg_con)
