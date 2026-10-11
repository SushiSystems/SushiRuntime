/**************************************************************************/
/* test_usm_registry.cpp                                                  */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_usm_registry.cpp
 * @brief Tests USMRegistry's record, take, owner and containing-allocation lookups.
 * @see modules/platform/memory/tests/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>
#include <SushiRuntime/memory/usm_registry.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

using SushiRuntime::Memory::USMRegistry;

namespace
{
    /** @brief Returns a distinct fake address for each n, 64 bytes apart. */
    void* fake(std::uintptr_t n) { return reinterpret_cast<void*>(n * 64u + 64u); }

    /**
     * @brief Holds the 64-byte extent that covers one fake address and never reaches its neighbour.
     */
    constexpr std::size_t EXTENT = 64;

    // An address inside fake(n)'s extent, for the interior-pointer cases.
    void* inside(std::uintptr_t n, std::size_t offset)
    {
        return static_cast<unsigned char*>(fake(n)) + offset;
    }
}

TEST(Unit_UsmRegistry, RecordThenOwnerOfReturnsTheDevice)
{
    USMRegistry registry;
    registry.record(fake(1), 3, EXTENT);

    const auto owner = registry.owner_of(fake(1));
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, 3u);
}

TEST(Unit_UsmRegistry, AnUnrecordedPointerHasNoOwner)
{
    USMRegistry registry;
    registry.record(fake(1), 0, EXTENT);

    EXPECT_FALSE(registry.owner_of(fake(2)).has_value());
    EXPECT_FALSE(registry.owner_of(nullptr).has_value());
}

TEST(Unit_UsmRegistry, TakeReportsTheOwnerAndForgetsThePointer)
{
    USMRegistry registry;
    registry.record(fake(1), 2, EXTENT);

    const auto taken = registry.take(fake(1));
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(*taken, 2u);

    // A second take of the same pointer must report no owner; see README.md.
    EXPECT_FALSE(registry.take(fake(1)).has_value());
    EXPECT_FALSE(registry.owner_of(fake(1)).has_value());
}

TEST(Unit_UsmRegistry, RecordingAPointerTwiceKeepsTheLatestOwner)
{
    // Recording a pointer twice keeps the last writer; see README.md.
    USMRegistry registry;
    registry.record(fake(1), 1, EXTENT);
    registry.record(fake(1), 4, EXTENT);

    const auto owner = registry.owner_of(fake(1));
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, 4u);
}

TEST(Unit_UsmRegistry, SizeCountsLiveEntries)
{
    USMRegistry registry;
    EXPECT_EQ(registry.size(), 0u);

    registry.record(fake(1), 0, EXTENT);
    registry.record(fake(2), 0, EXTENT);
    EXPECT_EQ(registry.size(), 2u);

    registry.take(fake(1));
    EXPECT_EQ(registry.size(), 1u);
}

TEST(Unit_UsmRegistry, ConcurrentRecordAndTakeLoseNothing)
{
    // Every pointer recorded here is taken exactly once, by exactly one thread; see README.md.
    constexpr int THREADS = 8;
    constexpr int PER_THREAD = 500;

    USMRegistry registry;
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
                registry.record(p, static_cast<std::size_t>(t), EXTENT);
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

TEST(Unit_UsmRegistry, ContainingAllocationResolvesAnInteriorPointer)
{
    // The caller passes an interior address the base-keyed table cannot find; see README.md.
    USMRegistry registry;
    registry.record(fake(1), 5, EXTENT);

    const auto found = registry.containing_allocation(inside(1, 16));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->base, fake(1));
    EXPECT_EQ(found->device_index, 5u);

    // The base itself and the last byte are both inside; one past the end is not.
    EXPECT_TRUE(registry.containing_allocation(fake(1)).has_value());
    EXPECT_TRUE(registry.containing_allocation(inside(1, EXTENT - 1)).has_value());
    EXPECT_FALSE(registry.containing_allocation(inside(1, EXTENT)).has_value());
}

TEST(Unit_UsmRegistry, ContainingAllocationIgnoresAnUnrelatedPointer)
{
    USMRegistry registry;
    registry.record(fake(1), 0, EXTENT);

    EXPECT_FALSE(registry.containing_allocation(fake(9)).has_value());
    EXPECT_FALSE(registry.containing_allocation(nullptr).has_value());
}

TEST(Unit_UsmRegistry, AnEntryWithoutALengthResolvesOnlyAtItsBase)
{
    // An entry without a length resolves only at its base; see README.md.
    USMRegistry registry;
    registry.record(fake(1), 7, std::nullopt);

    const auto base = registry.containing_allocation(fake(1));
    ASSERT_TRUE(base.has_value());
    EXPECT_EQ(base->device_index, 7u);
    EXPECT_FALSE(registry.containing_allocation(inside(1, 8)).has_value());
}

TEST(Unit_UsmRegistry, ContainingAllocationForgetsATakenPointer)
{
    USMRegistry registry;
    registry.record(fake(1), 0, EXTENT);
    registry.take(fake(1));

    EXPECT_FALSE(registry.containing_allocation(inside(1, 8)).has_value());
}
