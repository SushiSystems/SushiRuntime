/****************************************************************/
/* test_logger.cpp                                              */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_logger.cpp
 * @brief Tests that an info log formats without throwing and SR_THROW_IF throws only when true.
 * @author Mustafa Garip
 */

#include <string>
#include <iostream>
#include <gtest/gtest.h>
#include <SushiRuntime/core/logger.hpp>

using namespace SushiRuntime;

/**
 * @brief Tests basic info logging formatting.
 *
 * We cannot easily capture the log stream here, but we can verify formatting runs
 * without throwing exceptions.
 */
TEST(Unit_LoggerTest, BasicInfoLog)
{
    SR_LOGGER_SET_SYNC_MODE(true);
    EXPECT_NO_THROW({ SR_LOG_INFO("Testing logger: {}", "Success"); });
}

/**
 * @brief Tests that SR_THROW_IF throws on a true condition and stays silent on false.
 *
 * Validates the behavior of the throw-if macro.
 */
TEST(Unit_LoggerTest, ThrowIfMacro)
{
    EXPECT_THROW({ SR_THROW_IF(true, "Expected failure {}", 404); }, std::runtime_error);
    EXPECT_NO_THROW({ SR_THROW_IF(false, "Should not fail"); });
}
