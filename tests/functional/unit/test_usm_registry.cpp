/****************************************************************/
/* test_usm_registry.cpp                                        */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#include <gtest/gtest.h>
#include <SushiRuntime/memory/usm_registry.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

using SushiRuntime::Memory::UsmRegistry;

namespace
{
    // Registry keys are opaque; nothing is dereferenced, so distinct fake
    // addresses are enough and keep the test free of a Runtime.
    void* fake(std::uintptr_t n) { return reinterpret_cast<void*>(n * 64u + 64u); }

    // Fake addresses sit 64 bytes apart, so a 64-byte extent covers one of them
    // exactly and never reaches its neighbour.
    constexpr std::size_t EXTENT = 64;

    // An address inside fake(n)'s extent, for the interior-pointer cases.
    void* inside(std::uintptr_t n, std::size_t offset)
    {
        return static_cast<unsigned char*>(fake(n)) + offset;
    }
}

TEST(Unit_UsmRegistry, RecordThenOwnerOfReturnsTheDevice)
{
    UsmRegistry registry;
    registry.record(fake(1), 3, EXTENT);

    const auto owner = registry.owner_of(fake(1));
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, 3u);
}

TEST(Unit_UsmRegistry, AnUnrecordedPointerHasNoOwner)
{
    UsmRegistry registry;
    registry.record(fake(1), 0, EXTENT);

    EXPECT_FALSE(registry.owner_of(fake(2)).has_value());
    EXPECT_FALSE(registry.owner_of(nullptr).has_value());
}

TEST(Unit_UsmRegistry, TakeReportsTheOwnerAndForgetsThePointer)
{
    UsmRegistry registry;
    registry.record(fake(1), 2, EXTENT);

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
    registry.record(fake(1), 1, EXTENT);
    registry.record(fake(1), 4, EXTENT);

    const auto owner = registry.owner_of(fake(1));
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, 4u);
}

TEST(Unit_UsmRegistry, SizeCountsLiveEntries)
{
    UsmRegistry registry;
    EXPECT_EQ(registry.size(), 0u);

    registry.record(fake(1), 0, EXTENT);
    registry.record(fake(2), 0, EXTENT);
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
    // The case the graph builder needs: a caller offsets into a plan array and
    // hands over an address the base-keyed table cannot find.
    UsmRegistry registry;
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
    UsmRegistry registry;
    registry.record(fake(1), 0, EXTENT);

    EXPECT_FALSE(registry.containing_allocation(fake(9)).has_value());
    EXPECT_FALSE(registry.containing_allocation(nullptr).has_value());
}

TEST(Unit_UsmRegistry, AnEntryWithoutALengthResolvesOnlyAtItsBase)
{
    // An adopted address whose importer knew no size must not read as zero-length
    // nor swallow offsets it cannot vouch for.
    UsmRegistry registry;
    registry.record(fake(1), 7, std::nullopt);

    const auto base = registry.containing_allocation(fake(1));
    ASSERT_TRUE(base.has_value());
    EXPECT_EQ(base->device_index, 7u);
    EXPECT_FALSE(registry.containing_allocation(inside(1, 8)).has_value());
}

TEST(Unit_UsmRegistry, ContainingAllocationForgetsATakenPointer)
{
    UsmRegistry registry;
    registry.record(fake(1), 0, EXTENT);
    registry.take(fake(1));

    EXPECT_FALSE(registry.containing_allocation(inside(1, 8)).has_value());
}
