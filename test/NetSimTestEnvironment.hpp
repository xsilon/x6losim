/*
 * Author: Martin Townsend
 *             email: martin.townsend@xsilon.com
 *             email: mtownsend1973@gmail.com
 *             skype: mtownsend1973
 *
 * Description: todo
 *
 */
#include <gtest/gtest.h>
#include <stdlib.h>


#ifndef NETSIMTESTENVIRONMENT_HPP_
#define NETSIMTESTENVIRONMENT_HPP_



class NetSimTestEnvironment : public ::testing::Environment {
public:
    boolean m_libxml;
    int m_log_level;

    NetSimTestEnvironment() {
    }

    // Override this to define how to set up the environment.
    void SetUp() {
    }
    // Override this to define how to tear down the environment.
    void TearDown() {

    }
};


#endif /* SSMDTESTENVIRONMENT_HPP_ */
