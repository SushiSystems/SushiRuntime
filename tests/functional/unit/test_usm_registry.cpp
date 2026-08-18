/**************************************************************************/
/* test_usm_registry.cpp                                                  */
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

#include <gtest/gtest.h>
#include <SushiRuntime/memory/usm_registry.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using SushiRuntime::Memory::UsmRegistry;

namespace
{
    // Registry keys are opaque; nothing is dereferenced, so distinct fake
    // addresses are enough and keep the test free of a Runtime.
    void* fake(std::uintptr_t n) { return reinterpret_cast<void*>(n * 64u + 64u); }
}

TEST(Unit_UsmRegistry, RecordThenOwnerOfReturnsTheDevice)
{
    UsmRegistry registry;
    registry.record(fake(1), 3);

    const auto owner = registry.owner_of(fake(1));
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, 3u);
}

TEST(Unit_UsmRegistry, AnUnrecordedPointerHasNoOwner)
{
    UsmRegistry registry;
    registry.record(fake(1), 0);

    EXPECT_FALSE(registry.owner_of(fake(2)).has_value());
    EXPECT_FALSE(registry.owner_of(nullptr).has_value());
}

TEST(Unit_UsmRegistry, TakeReportsTheOwnerAndForgetsThePointer)
{
    UsmRegistry registry;
    registry.record(fake(1), 2);

    const auto taken = registry.take(fake(1));
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(*taken, 2u);

    // The free path erases and reads the owner in one step, so a second free of
    // the same pointer must be distinguishable from the first rather than
    // reporting a stale device.
    EXPECT_FALSE(registry.take(fake(1)).has_value());
    EXPECT_FALSE(registry.owner_of(fake(1)).has_value());
}

TEST(Unit_UsmRegistry, RecordingAPointerTwiceKeepsTheLatestOwner)
{
    // Only reachable if an allocator handed back an address it had not been told
    // was freed. Last writer wins, which matches the map assignment the runtime
    // has always done, and is asserted so a future change to the container does
    // not quietly turn it into first-writer-wins.
    UsmRegistry registry;
    registry.record(fake(1), 1);
    registry.record(fake(1), 4);

    const auto owner = registry.owner_of(fake(1));
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, 4u);
}

TEST(Unit_UsmRegistry, SizeCountsLiveEntries)
{
    UsmRegistry registry;
    EXPECT_EQ(registry.size(), 0u);

    registry.record(fake(1), 0);
    registry.record(fake(2), 0);
    EXPECT_EQ(registry.size(), 2u);

    registry.take(fake(1));
    EXPECT_EQ(registry.size(), 1u);
}

TEST(Unit_UsmRegistry, ConcurrentRecordAndTakeLoseNothing)
{
    // Buffers are constructed and destroyed from whichever thread owns them, so
    // the registry is written from several at once. Every pointer recorded here
    // is taken exactly once, by exactly one thread.
    constexpr int THREADS = 8;
    constexpr int PER_THREAD = 500;

    UsmRegistry registry;
    std::atomic<int> taken{0};
    std::vector<std::thread> workers;
    workers.reserve(THREADS);

    for (int t = 0; t < THREADS; ++t)
    {
        workers.emplace_back([&registry, &taken, t]
        {
            for (int i = 0; i < PER_THREAD; ++i)
            {
                void* p = fake(static_cast<std::uintptr_t>(t * PER_THREAD + i + 1));
                registry.record(p, static_cast<std::size_t>(t));
                if (auto owner = registry.take(p))
                {
                    EXPECT_EQ(*owner, static_cast<std::size_t>(t));
                    taken.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& w : workers)
        w.join();

    EXPECT_EQ(taken.load(), THREADS * PER_THREAD);
    EXPECT_EQ(registry.size(), 0u);
}
