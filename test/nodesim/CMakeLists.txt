
#ADD_EXECUTABLE (connect_no_reg_con common.cpp connect_no_reg_con.cpp)
#TARGET_LINK_LIBRARIES(connect_no_reg_con ${LIBS})

ADD_EXECUTABLE (TxOnePacket common.cpp TxOnePacket.cpp)
TARGET_LINK_LIBRARIES(TxOnePacket ${LIBS})
