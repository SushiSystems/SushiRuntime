/**************************************************************************/
/* test_buffer_wire_id.cpp                                                */
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
 * @file test_buffer_wire_id.cpp
 * @brief Tests wire_id_of(): the wire id is a pure function of the handle.
 * @see tests/extension/buffer_exchange/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/buffer_exchange/buffer_wire_id.hpp>
#include <SushiRuntime/handle/global_handle.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Returns a live buffer handle of the given generation and slot. */
    GlobalHandle handle_of(std::uint32_t generation, std::uint32_t slot)
    {
        return GlobalHandle(1, 0, HandleKind::Buffer, HandleFlags::None, generation, slot);
    }
} // namespace

TEST(Small_BufferWireIDTest, TheSameHandleGivesTheSameID)
{
    EXPECT_EQ(wire_id_of(handle_of(3, 9)), wire_id_of(handle_of(3, 9)));
}

TEST(Small_BufferWireIDTest, ASlotMintedAgainGivesANewID)
{
    EXPECT_NE(wire_id_of(handle_of(1, 9)), wire_id_of(handle_of(2, 9)));
}

TEST(Small_BufferWireIDTest, TwoSlotsGiveTwoIDs)
{
    EXPECT_NE(wire_id_of(handle_of(1, 8)), wire_id_of(handle_of(1, 9)));
}

TEST(Small_BufferWireIDTest, ALiveHandleGivesAValidIDAndTheNullHandleDoesNot)
{
    EXPECT_TRUE(wire_id_of(handle_of(1, 0)).valid());
    EXPECT_FALSE(wire_id_of(GlobalHandle::null()).valid());
}
