/**************************************************************************/
/* test_transport.cpp                                                     */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_transport.cpp
 * @brief Tests TransportResult: only Ok counts as a started transport.
 * @see tests/extension/transport/README.md
 * @author Mustafa Garip
 */

#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/transport/transport.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Holds a start status and whether it counts as a started transport. */
    struct StatusRow
    {
        const char* meaning;   /**< Names the status in a test report. */
        TransportStatus status; /**< The status of the result. */
        bool started;           /**< Whether is_ok() must answer true. */
    };

    /** @brief Names a row by its meaning in a test report. */
    std::string name_of(const ::testing::TestParamInfo<StatusRow>& info)
    {
        return info.param.meaning;
    }

    const StatusRow K_STATUSES[] = {
        {"Ok", TransportStatus::Ok, true},
        {"AlreadyStarted", TransportStatus::AlreadyStarted, false},
        {"InvalidArgument", TransportStatus::InvalidArgument, false},
        {"BindFailed", TransportStatus::BindFailed, false},
        {"Failed", TransportStatus::Failed, false},
    };
} // namespace

/** @brief Runs a test once per start status. */
class Small_TransportResult : public ::testing::TestWithParam<StatusRow>
{
};

/** @brief Tests that is_ok() is true for Ok and false for every other status. */
TEST_P(Small_TransportResult, IsOkOnlyForOk)
{
    TransportResult result;
    result.status = GetParam().status;

    EXPECT_EQ(result.is_ok(), GetParam().started);
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_TransportResult, ::testing::ValuesIn(K_STATUSES), name_of);
