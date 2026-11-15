/**************************************************************************/
/* test_handle_ownership.cpp                                              */
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
 * @file test_handle_ownership.cpp
 * @brief Tests HandleOwnership: the owner is the minting machine, and foreign handles are refused.
 * @author Mustafa Garip
 * @see tests/extension/offload_coordination/README.md
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/offload_coordination/handle_ownership.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Returns a handle minted on @p machine at slot @p slot. */
    GlobalHandle minted_on(std::uint16_t machine, std::uint32_t slot)
    {
        return GlobalHandle(1, machine, HandleKind::Buffer, HandleFlags::None, 1, slot);
    }
} // namespace

/** @brief Tests that a handle this machine minted is owned. */
TEST(Small_HandleOwnership, AnOwnedHandleIsOwned)
{
    EXPECT_EQ(classify_ownership(minted_on(K_MASTER_MACHINE, 4), K_MASTER_MACHINE),
              OwnershipStatus::Owned);
}

/** @brief Tests that a handle another machine minted is foreign. */
TEST(Small_HandleOwnership, AForeignHandleIsForeign)
{
    EXPECT_EQ(classify_ownership(minted_on(3, 4), K_MASTER_MACHINE), OwnershipStatus::Foreign);
}

/** @brief Tests that the null handle has no owner. */
TEST(Small_HandleOwnership, TheNullHandleHasNoOwner)
{
    EXPECT_EQ(classify_ownership(GlobalHandle::null(), K_MASTER_MACHINE), OwnershipStatus::Null);
}

/** @brief Tests that the answer depends on the two arguments only: repeats give the same status. */
TEST(Small_HandleOwnership, ClassifyIsAPureFunctionOfItsArguments)
{
    const GlobalHandle owned = minted_on(2, 9);
    const GlobalHandle foreign = minted_on(0, 9);

    EXPECT_EQ(classify_ownership(owned, 2), classify_ownership(owned, 2));
    EXPECT_EQ(classify_ownership(owned, 2), OwnershipStatus::Owned);
    EXPECT_EQ(classify_ownership(foreign, 2), OwnershipStatus::Foreign);
    EXPECT_EQ(classify_ownership(foreign, 0), OwnershipStatus::Owned);
}
