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

/** @brief Tests that a handle this machine minted is recorded with this machine as owner. */
TEST(Small_HandleOwnership, AnOwnedHandleIsRecordedWithItsMachine)
{
    HandleOwnership ownership(K_MASTER_MACHINE);
    const GlobalHandle handle = minted_on(K_MASTER_MACHINE, 4);

    EXPECT_EQ(ownership.admit(handle), OwnershipStatus::Owned);
    ASSERT_TRUE(ownership.owner_of(handle).has_value());
    EXPECT_EQ(*ownership.owner_of(handle), K_MASTER_MACHINE);
}

/** @brief Tests that a handle another machine minted is refused and not recorded. */
TEST(Small_HandleOwnership, AForeignHandleIsRefused)
{
    HandleOwnership ownership(K_MASTER_MACHINE);
    const GlobalHandle handle = minted_on(3, 4);

    EXPECT_EQ(ownership.admit(handle), OwnershipStatus::Foreign);
    EXPECT_FALSE(ownership.owner_of(handle).has_value());
}

/** @brief Tests that the null handle has no owner. */
TEST(Small_HandleOwnership, TheNullHandleHasNoOwner)
{
    HandleOwnership ownership(K_MASTER_MACHINE);

    EXPECT_EQ(ownership.admit(GlobalHandle::null()), OwnershipStatus::Null);
    EXPECT_FALSE(ownership.owner_of(GlobalHandle::null()).has_value());
}

/** @brief Tests that the answer depends on the handle only: the same handle, the same status. */
TEST(Small_HandleOwnership, AdmitIsAPureFunctionOfTheHandle)
{
    HandleOwnership first(2);
    HandleOwnership second(2);
    const GlobalHandle owned = minted_on(2, 9);
    const GlobalHandle foreign = minted_on(0, 9);

    EXPECT_EQ(first.admit(owned), second.admit(owned));
    EXPECT_EQ(first.admit(owned), OwnershipStatus::Owned);
    EXPECT_EQ(first.admit(foreign), second.admit(foreign));
    EXPECT_EQ(first.local_machine(), 2u);
}

/** @brief Tests that a forgotten handle has no owner until it is admitted again. */
TEST(Small_HandleOwnership, ForgottenHandleHasNoOwner)
{
    HandleOwnership ownership(K_MASTER_MACHINE);
    const GlobalHandle handle = minted_on(K_MASTER_MACHINE, 1);
    ASSERT_EQ(ownership.admit(handle), OwnershipStatus::Owned);

    ownership.forget(handle);
    EXPECT_FALSE(ownership.owner_of(handle).has_value());
}
