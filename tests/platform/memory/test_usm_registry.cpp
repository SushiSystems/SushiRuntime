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
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/usm_registry.hpp>

using SushiRuntime::Internal::Memory::USMRegistry;

namespace
{
    /** @brief Returns a distinct fake address for each n, 64 bytes apart. */
    void* fake(std::uintptr_t n) { return reinterpret_cast<void*>(n * 64u + 64u); }

    /**
     * @brief Holds the 64-byte extent that covers one fake address and never reaches its neighbour.
     */
    constexpr std::size_t K_EXTENT = 64;

    // An address inside fake(n)'s extent, for the interior-pointer cases.
    void* inside(std::uintptr_t n, std::size_t offset)
    {
        return static_cast<unsigned char*>(fake(n)) + offset;
    }
}

TEST(Medium_UsmRegistry, RecordThenOwnerOfReturnsTheDevice)
{
    USMRegistry registry;
    registry.record(fake(1), 3, K_EXTENT, std::nullopt);

    const auto owner = registry.owner_of(fake(1));
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, 3u);
}

TEST(Medium_UsmRegistry, AnUnrecordedPointerHasNoOwner)
{
    USMRegistry registry;
    registry.record(fake(1), 0, K_EXTENT, std::nullopt);

    EXPECT_FALSE(registry.owner_of(fake(2)).has_value());
    EXPECT_FALSE(registry.owner_of(nullptr).has_value());
}

TEST(Medium_UsmRegistry, TakeReportsTheOwnerAndForgetsThePointer)
{
    USMRegistry registry;
    registry.record(fake(1), 2, K_EXTENT, std::nullopt);

    const auto taken = registry.take(fake(1));
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(*taken, 2u);

    // A second take of the same pointer must report no owner; see README.md.
    EXPECT_FALSE(registry.take(fake(1)).has_value());
    EXPECT_FALSE(registry.owner_of(fake(1)).has_value());
}

TEST(Medium_UsmRegistry, RecordingAPointerTwiceKeepsTheLatestOwner)
{
    // Recording a pointer twice keeps the last writer; see README.md.
    USMRegistry registry;
    registry.record(fake(1), 1, K_EXTENT, std::nullopt);
    registry.record(fake(1), 4, K_EXTENT, std::nullopt);

    const auto owner = registry.owner_of(fake(1));
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, 4u);
}

TEST(Medium_UsmRegistry, SizeCountsLiveEntries)
{
    USMRegistry registry;
    EXPECT_EQ(registry.size(), 0u);

    registry.record(fake(1), 0, K_EXTENT, std::nullopt);
    registry.record(fake(2), 0, K_EXTENT, std::nullopt);
    EXPECT_EQ(registry.size(), 2u);

    registry.take(fake(1));
    EXPECT_EQ(registry.size(), 1u);
}

TEST(Medium_UsmRegistry, ConcurrentRecordAndTakeLoseNothing)
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
                registry.record(p, static_cast<std::size_t>(t), K_EXTENT, std::nullopt);
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

TEST(Medium_UsmRegistry, RecordOfReportsTheDeviceAndKindAtTheBaseOnly)
{
    USMRegistry registry;
    registry.record(fake(1), 2, K_EXTENT, SushiRuntime::Internal::Memory::USMKind::Host);
    registry.record(fake(2), 3, K_EXTENT, std::nullopt);

    const auto made = registry.record_of(fake(1));
    ASSERT_TRUE(made.has_value());
    EXPECT_EQ(made->device_index, 2u);
    EXPECT_EQ(made->kind, SushiRuntime::Internal::Memory::USMKind::Host);

    const auto adopted = registry.record_of(fake(2));
    ASSERT_TRUE(adopted.has_value());
    EXPECT_FALSE(adopted->kind.has_value());

    EXPECT_FALSE(registry.record_of(inside(1, 16)).has_value());
    EXPECT_FALSE(registry.record_of(nullptr).has_value());
}

TEST(Medium_UsmRegistry, ContainingAllocationResolvesAnInteriorPointer)
{
    // The caller passes an interior address the base-keyed table cannot find; see README.md.
    USMRegistry registry;
    registry.record(fake(1), 5, K_EXTENT, std::nullopt);

    const auto found = registry.containing_allocation(inside(1, 16));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->base, fake(1));
    EXPECT_EQ(found->device_index, 5u);

    // The base itself and the last byte are both inside; one past the end is not.
    EXPECT_TRUE(registry.containing_allocation(fake(1)).has_value());
    EXPECT_TRUE(registry.containing_allocation(inside(1, K_EXTENT - 1)).has_value());
    EXPECT_FALSE(registry.containing_allocation(inside(1, K_EXTENT)).has_value());
}

TEST(Medium_UsmRegistry, ContainingAllocationIgnoresAnUnrelatedPointer)
{
    USMRegistry registry;
    registry.record(fake(1), 0, K_EXTENT, std::nullopt);

    EXPECT_FALSE(registry.containing_allocation(fake(9)).has_value());
    EXPECT_FALSE(registry.containing_allocation(nullptr).has_value());
}

TEST(Medium_UsmRegistry, AnEntryWithoutALengthResolvesOnlyAtItsBase)
{
    // An entry without a length resolves only at its base; see README.md.
    USMRegistry registry;
    registry.record(fake(1), 7, std::nullopt, std::nullopt);

    const auto base = registry.containing_allocation(fake(1));
    ASSERT_TRUE(base.has_value());
    EXPECT_EQ(base->device_index, 7u);
    EXPECT_FALSE(registry.containing_allocation(inside(1, 8)).has_value());
}

TEST(Medium_UsmRegistry, ContainingAllocationForgetsATakenPointer)
{
    USMRegistry registry;
    registry.record(fake(1), 0, K_EXTENT, std::nullopt);
    registry.take(fake(1));

    EXPECT_FALSE(registry.containing_allocation(inside(1, 8)).has_value());
}
