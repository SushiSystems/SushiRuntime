/**************************************************************************/
/* test_logger.cpp                                                        */
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

#include <string>
#include <iostream>
#include <gtest/gtest.h>
#include <SushiRuntime/core/logger.hpp>

using namespace SushiRuntime;

TEST(Unit_LoggerTest, BasicInfoLog)
{
    // We can't easily capture stdout/stderr from GTest without some redirection,
    // but we can at least verify that the format doesn't throw and it runs.
    SR_LOGGER_SET_SYNC_MODE(true);

    EXPECT_NO_THROW(
        {
        SR_LOG_INFO("Testing logger: {}", "Success");
    });
}

TEST(Unit_LoggerTest, ThrowIfMacro)
{
    EXPECT_THROW({
        SR_THROW_IF(true, "Expected failure {}", 404);
    }, std::runtime_error);

    EXPECT_NO_THROW({
        SR_THROW_IF(false, "Should not fail");
    });
}
