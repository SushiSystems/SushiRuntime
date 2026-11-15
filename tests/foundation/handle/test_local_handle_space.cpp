/**************************************************************************/
/* test_local_handle_space.cpp                                            */
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
 * @file test_local_handle_space.cpp
 * @brief Tests LocalHandleSpace minting in the reserved local universe.
 * @see tests/foundation/handle/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/local_handle_space.hpp>

using SushiRuntime::GlobalHandle;
using SushiRuntime::HandleFlags;
using SushiRuntime::HandleKind;
using SushiRuntime::HandleStatus;
using SushiRuntime::K_LOCAL_HANDLE_UNIVERSE;
using SushiRuntime::LocalHandleSpace;
using SushiRuntime::MintOutcome;

namespace
{
    TEST(Small_LocalHandleSpace, MintsInSlotOrderInTheLocalUniverseWithHostOnlyBorrowedFlags)
    {
        LocalHandleSpace space(5);

        for (std::uint32_t expected = 0; expected < 3; ++expected)
        {
            const MintOutcome outcome = space.mint(HandleKind::Buffer);
            ASSERT_EQ(outcome.status, HandleStatus::Ok);
            EXPECT_EQ(outcome.handle.universe(), K_LOCAL_HANDLE_UNIVERSE);
            EXPECT_EQ(outcome.handle.machine(), 5);
            EXPECT_EQ(outcome.handle.slot(), expected);
            EXPECT_EQ(outcome.handle.generation(), 1U);
            EXPECT_FALSE(outcome.handle.is_null());
            EXPECT_TRUE(LocalHandleSpace::is_local(outcome.handle));
            EXPECT_TRUE(has_flags(outcome.handle.flags(),
                                  HandleFlags::HostOnly | HandleFlags::Borrowed));
            EXPECT_FALSE(has_flags(outcome.handle.flags(), HandleFlags::DoNotCache));
        }
    }

    TEST(Small_LocalHandleSpace, TheSameSequenceOfMintsGivesTheSameHandles)
    {
        LocalHandleSpace first(2);
        LocalHandleSpace second(2);

        for (int i = 0; i < 8; ++i)
            EXPECT_EQ(first.mint(HandleKind::State).handle, second.mint(HandleKind::State).handle);
    }

    TEST(Small_LocalHandleSpace, SpacesWithDifferentIDsMintDistinctHandles)
    {
        LocalHandleSpace first(1);
        LocalHandleSpace second(2);

        EXPECT_NE(first.mint(HandleKind::Buffer).handle, second.mint(HandleKind::Buffer).handle);
        EXPECT_EQ(first.space_id(), 1);
        EXPECT_EQ(second.space_id(), 2);
    }

    TEST(Small_LocalHandleSpace, ConcurrentMintsGiveEveryHandleOnce)
    {
        constexpr int K_THREADS = 4;
        constexpr int K_PER_THREAD = 1000;
        LocalHandleSpace space(0);
        std::vector<std::vector<GlobalHandle>> minted(K_THREADS);

        std::vector<std::thread> threads;
        for (int t = 0; t < K_THREADS; ++t)
        {
            threads.emplace_back([&space, &minted, t]()
            {
                for (int i = 0; i < K_PER_THREAD; ++i)
                    minted[t].push_back(space.mint(HandleKind::Buffer).handle);
            });
        }
        for (std::thread& thread : threads)
            thread.join();

        std::set<GlobalHandle> distinct;
        for (const std::vector<GlobalHandle>& handles : minted)
            distinct.insert(handles.begin(), handles.end());
        EXPECT_EQ(distinct.size(), static_cast<std::size_t>(K_THREADS * K_PER_THREAD));
        EXPECT_EQ(distinct.begin()->slot(), 0U);
        EXPECT_EQ(distinct.rbegin()->slot(), static_cast<std::uint32_t>(K_THREADS * K_PER_THREAD - 1));
    }
}
