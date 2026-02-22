/**************************************************************************/
/* test_main.cpp                                                          */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#include <gtest/gtest.h>
#include <SushiRuntime/core/logger.hpp>

int main(int argc, char **argv) 
{
    // Force logs to stream synchronously during testing to avoid overlap with GTest output.
    SR_LOGGER_SET_SYNC_MODE(false);
    
    ::testing::InitGoogleTest(&argc, argv);
    
    int res = RUN_ALL_TESTS();
    SR_LOG_INFO("All tests finished with result: {}", res);
    return res;
}
