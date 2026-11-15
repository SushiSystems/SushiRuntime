/**************************************************************************/
/* test_handle_space.cpp                                                  */
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
 * @file test_handle_space.cpp
 * @brief Tests the HandleSpace contract through a small fake space.
 * @see tests/foundation/handle/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/handle_space.hpp>

using SushiRuntime::GlobalHandle;
using SushiRuntime::HandleFlags;
using SushiRuntime::HandleKind;
using SushiRuntime::HandleSpace;
using SushiRuntime::HandleStatus;
using SushiRuntime::MintOutcome;

namespace
{
    /** @brief Mints from a fixed number of slots, lowest free slot first. */
    class FakeHandleSpace final : public HandleSpace
    {
        public:
            /** @brief Builds a space of universe with slot_count slots. */
            FakeHandleSpace(std::uint32_t universe, std::uint32_t slot_count)
                : universe_(universe), generations_(slot_count, 0), live_(slot_count, false)
            {

            }

            /** @brief Mints in the lowest free slot under its next generation. */
            MintOutcome mint(HandleKind kind, HandleFlags flags) override
            {
                for (std::uint32_t slot = 0; slot < live_.size(); ++slot)
                {
                    if (live_[slot])
                        continue;
                    live_[slot] = true;
                    ++generations_[slot];
                    return {GlobalHandle(universe_, 0, kind, flags, generations_[slot], slot),
                            HandleStatus::Ok};
                }
                return {GlobalHandle::null(), HandleStatus::SpaceExhausted};
            }

            /** @brief Frees the handle's slot when the handle is current. */
            HandleStatus retire(GlobalHandle handle) override
            {
                if (handle.is_null())
                    return HandleStatus::NullHandle;
                if (handle.universe() != universe_)
                    return HandleStatus::ForeignUniverse;
                if (handle.slot() >= live_.size() || !live_[handle.slot()] ||
                    generations_[handle.slot()] != handle.generation())
                    return HandleStatus::StaleHandle;
                live_[handle.slot()] = false;
                return HandleStatus::Ok;
            }

        private:
            /// @brief Universe every minted handle carries.
            std::uint32_t universe_;

            /// @brief Current generation of each slot.
            std::vector<std::uint32_t> generations_;

            /// @brief Whether each slot holds a live handle.
            std::vector<bool> live_;
    };
}

TEST(Small_HandleSpace, MintsDistinctHandlesInSlotOrderUntilExhausted)
{
    FakeHandleSpace fake(5, 3);
    HandleSpace& space = fake;
    std::set<GlobalHandle> minted;
    for (std::uint32_t slot = 0; slot < 3; ++slot)
    {
        const MintOutcome outcome = space.mint(HandleKind::Buffer, HandleFlags::Borrowed);
        ASSERT_EQ(outcome.status, HandleStatus::Ok);
        EXPECT_FALSE(outcome.handle.is_null());
        EXPECT_EQ(outcome.handle.slot(), slot);
        EXPECT_EQ(outcome.handle.universe(), 5U);
        EXPECT_EQ(outcome.handle.flags(), HandleFlags::Borrowed);
        minted.insert(outcome.handle);
    }
    EXPECT_EQ(minted.size(), 3U);

    const MintOutcome exhausted = space.mint(HandleKind::Buffer, HandleFlags::None);
    EXPECT_EQ(exhausted.status, HandleStatus::SpaceExhausted);
    EXPECT_TRUE(exhausted.handle.is_null());
}

TEST(Small_HandleSpace, RetireReusesTheSlotUnderAHigherGeneration)
{
    FakeHandleSpace fake(1, 1);
    HandleSpace& space = fake;
    const GlobalHandle first = space.mint(HandleKind::State, HandleFlags::None).handle;
    EXPECT_EQ(space.retire(first), HandleStatus::Ok);
    const GlobalHandle second = space.mint(HandleKind::State, HandleFlags::None).handle;
    EXPECT_EQ(second.slot(), first.slot());
    EXPECT_GT(second.generation(), first.generation());
    EXPECT_NE(second, first);
}

TEST(Small_HandleSpace, RetireRefusesNullStaleAndForeignHandles)
{
    FakeHandleSpace fake(1, 2);
    HandleSpace& space = fake;
    const GlobalHandle handle = space.mint(HandleKind::Buffer, HandleFlags::None).handle;
    const GlobalHandle foreign(2, 0, HandleKind::Buffer, HandleFlags::None, handle.generation(),
                               handle.slot());
    EXPECT_EQ(space.retire(GlobalHandle::null()), HandleStatus::NullHandle);
    EXPECT_EQ(space.retire(foreign), HandleStatus::ForeignUniverse);
    EXPECT_EQ(space.retire(handle), HandleStatus::Ok);
    EXPECT_EQ(space.retire(handle), HandleStatus::StaleHandle);
}
