/**************************************************************************/
/* test_logger.cpp                                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#include <string>
#include <iostream>
#include <gtest/gtest.h>
#include <SushiRuntime/core/logger.hpp>

using namespace SushiRuntime;

// We can't easily capture the log stream here, but we can verify formatting runs.
TEST(Unit_LoggerTest, BasicInfoLog)
{
    SR_LOGGER_SET_SYNC_MODE(true);
    EXPECT_NO_THROW({ SR_LOG_INFO("Testing logger: {}", "Success"); });
}

// SR_THROW_IF throws on a true condition and stays silent on a false one.
TEST(Unit_LoggerTest, ThrowIfMacro)
{
    EXPECT_THROW({ SR_THROW_IF(true, "Expected failure {}", 404); }, std::runtime_error);
    EXPECT_NO_THROW({ SR_THROW_IF(false, "Should not fail"); });
}
