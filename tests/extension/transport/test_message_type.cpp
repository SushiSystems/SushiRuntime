/**************************************************************************/
/* test_message_type.cpp                                                  */
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
 * @file test_message_type.cpp
 * @brief Tests MessageType: which bytes name a type and what a byte that names none is called.
 * @see tests/extension/transport/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/transport/message_type.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Holds a byte and whether it names a message type. */
    struct ByteRow
    {
        int byte;       /**< The byte offered to is_message_type(). */
        bool is_a_type; /**< Whether it names a type. */
    };

    /** @brief Names a row by its byte in a test report. */
    std::string name_of(const ::testing::TestParamInfo<ByteRow>& info)
    {
        return "Byte" + std::to_string(info.param.byte);
    }

    const ByteRow K_BYTES[] = {{0, false}, {1, true}, {16, true}, {17, false}};
} // namespace

/** @brief Runs a test once per byte at the edge of the type range. */
class Small_MessageTypeByte : public ::testing::TestWithParam<ByteRow>
{
};

/** @brief Tests that the types run from 1 to 16 and only those bytes name a type. */
TEST_P(Small_MessageTypeByte, IsAMessageTypeOnlyFromOneToSixteen)
{
    EXPECT_EQ(is_message_type(GetParam().byte), GetParam().is_a_type);
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_MessageTypeByte, ::testing::ValuesIn(K_BYTES), name_of);

/** @brief Runs a test once per byte that names no type. */
class Small_MessageTypeUnknownByte : public ::testing::TestWithParam<int>
{
};

/** @brief Tests that a byte outside the enumeration is named UNKNOWN. */
TEST_P(Small_MessageTypeUnknownByte, ANameIsUnknown)
{
    EXPECT_STREQ(message_type_name(static_cast<MessageType>(GetParam())), "UNKNOWN");
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_MessageTypeUnknownByte, ::testing::Values(0, 200));
