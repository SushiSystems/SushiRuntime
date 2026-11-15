/**************************************************************************/
/* test_handle_status.cpp                                                 */
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
 * @file test_handle_status.cpp
 * @brief Tests that every HandleStatus value is distinct and that Ok is zero.
 * @see tests/foundation/handle/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <set>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/handle_status.hpp>

using SushiRuntime::HandleStatus;

static_assert(std::is_same<std::underlying_type<HandleStatus>::type, std::uint8_t>::value,
              "a HandleStatus is one byte");

TEST(Small_HandleStatus, EveryValueIsDistinctAndOkIsZero)
{
    const std::vector<HandleStatus> statuses = {
        HandleStatus::Ok,          HandleStatus::NullHandle,
        HandleStatus::StaleHandle, HandleStatus::ForeignUniverse,
        HandleStatus::NotResident, HandleStatus::SpaceExhausted};
    std::set<std::uint8_t> values;
    for (const HandleStatus status : statuses)
        values.insert(static_cast<std::uint8_t>(status));
    EXPECT_EQ(values.size(), statuses.size());
    EXPECT_EQ(static_cast<std::uint8_t>(HandleStatus::Ok), 0U);
    EXPECT_EQ(HandleStatus{}, HandleStatus::Ok);
}
