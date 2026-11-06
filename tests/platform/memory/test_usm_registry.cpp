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
 * @file test_usm_registry.cpp
 * @brief Tests USMRegistry's record, take, owner and containing-allocation lookups.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
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

    /** @brief One lookup of an address against the allocation recorded at fake(1), and whether it finds it. */
    struct ContainingRow
    {
        /** @brief The row name gtest prints. */
        const char* name;
        /** @brief The fake address the lookup is made on; zero names null. */
        std::uintptr_t unit;
        /** @brief The offset into that address's extent. */
        std::size_t offset;
        /** @brief Whether the lookup finds the allocation. */
        bool found;
    };

    /** @brief One lookup that must find nothing, and what it looks up. */
    struct ForgottenRow
    {
        /** @brief The row name gtest prints. */
        const char* name;
        /** @brief Returns whether the lookup finds the pointer recorded at fake(1). */
        std::function<bool(USMRegistry&)> finds;
    };

    /** @brief What the threads of a record-and-take run took, and what they should have taken. */
    struct TakenOwners
    {
        /** @brief The owner each take returned, thread by thread. */
        std::vector<std::optional<std::size_t>> actual;
        /** @brief The owner each take should return: the thread that recorded it. */
        std::vector<std::optional<std::size_t>> expected;
    };

    /** @brief Records and takes @p per_thread pointers on each of @p threads threads, and collects each take's owner. */
    TakenOwners record_and_take_from_threads(USMRegistry& registry, int threads, int per_thread)
    {
        std::vector<std::vector<std::optional<std::size_t>>> by_thread(static_cast<std::size_t>(threads));
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(threads));
        for (int t = 0; t < threads; ++t)
        {
            workers.emplace_back([&registry, &by_thread, t, per_thread]
            {
                for (int i = 0; i < per_thread; ++i)
                {
                    void* p = fake(static_cast<std::uintptr_t>(t * per_thread + i + 1));
                    registry.record(p, static_cast<std::size_t>(t), K_EXTENT, std::nullopt);
                    by_thread[static_cast<std::size_t>(t)].push_back(registry.take(p));
                }
            });
        }
        for (auto& w : workers)
            w.join();

        TakenOwners owners;
        for (int t = 0; t < threads; ++t)
        {
            for (const std::optional<std::size_t>& taken : by_thread[static_cast<std::size_t>(t)])
            {
                owners.actual.push_back(taken);
                owners.expected.push_back(static_cast<std::size_t>(t));
            }
        }
        return owners;
    }
}

TEST(Small_UsmRegistry, RecordThenOwnerOfReturnsTheDevice)
{
    USMRegistry registry;
    registry.record(fake(1), 3, K_EXTENT, std::nullopt);

    const auto owner = registry.owner_of(fake(1));
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, 3u);
}

TEST(Small_UsmRegistry, AnUnrecordedPointerHasNoOwner)
{
    USMRegistry registry;
    registry.record(fake(1), 0, K_EXTENT, std::nullopt);

    EXPECT_FALSE(registry.owner_of(fake(2)).has_value());
    EXPECT_FALSE(registry.owner_of(nullptr).has_value());
}

TEST(Small_UsmRegistry, TakeReportsTheOwner)
{
    USMRegistry registry;
    registry.record(fake(1), 2, K_EXTENT, std::nullopt);

    const auto taken = registry.take(fake(1));

    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(*taken, 2u);
}

class Small_UsmRegistryForgotten : public ::testing::TestWithParam<ForgottenRow>
{
};

TEST_P(Small_UsmRegistryForgotten, ATakenPointerIsFoundByNoLookup)
{
    USMRegistry registry;
    registry.record(fake(1), 2, K_EXTENT, std::nullopt);
    (void)registry.take(fake(1));

    EXPECT_FALSE(GetParam().finds(registry));
}

INSTANTIATE_TEST_SUITE_P(
    Lookups, Small_UsmRegistryForgotten,
    ::testing::Values(
        ForgottenRow{"TakeAgain", [](USMRegistry& registry) { return registry.take(fake(1)).has_value(); }},
        ForgottenRow{"OwnerOf", [](USMRegistry& registry) { return registry.owner_of(fake(1)).has_value(); }},
        ForgottenRow{"ContainingAllocation",
                     [](USMRegistry& registry) { return registry.containing_allocation(inside(1, 8)).has_value(); }}),
    [](const ::testing::TestParamInfo<ForgottenRow>& info) { return std::string(info.param.name); });

TEST(Small_UsmRegistry, RecordingAPointerTwiceKeepsTheLatestOwner)
{
    // Recording a pointer twice keeps the last writer; see README.md.
    USMRegistry registry;
    registry.record(fake(1), 1, K_EXTENT, std::nullopt);
    registry.record(fake(1), 4, K_EXTENT, std::nullopt);

    const auto owner = registry.owner_of(fake(1));
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, 4u);
}

TEST(Small_UsmRegistry, SizeCountsLiveEntries)
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
    USMRegistry registry;

    const TakenOwners owners = record_and_take_from_threads(registry, 8, 500);

    EXPECT_EQ(owners.actual, owners.expected);
    EXPECT_EQ(registry.size(), 0u);
}

TEST(Small_UsmRegistry, RecordOfReportsTheDeviceAndKindAtTheBaseOnly)
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

class Small_UsmRegistryContaining : public ::testing::TestWithParam<ContainingRow>
{
};

TEST_P(Small_UsmRegistryContaining, FindsTheAllocationOnlyInsideItsExtent)
{
    // The caller passes an interior address the base-keyed table cannot find; see README.md.
    const ContainingRow& row = GetParam();
    USMRegistry registry;
    registry.record(fake(1), 5, K_EXTENT, std::nullopt);
    const void* const address = row.unit == 0 ? nullptr : inside(row.unit, row.offset);

    const auto found = registry.containing_allocation(address);

    EXPECT_EQ(found.has_value(), row.found);
    if (found.has_value())
    {
        EXPECT_EQ(found->base, fake(1));
        EXPECT_EQ(found->device_index, 5u);
    }
}

INSTANTIATE_TEST_SUITE_P(Addresses, Small_UsmRegistryContaining,
                         ::testing::Values(ContainingRow{"AnInteriorByte", 1, 16, true},
                                           ContainingRow{"TheBase", 1, 0, true},
                                           ContainingRow{"TheLastByte", 1, K_EXTENT - 1, true},
                                           ContainingRow{"OnePastTheEnd", 1, K_EXTENT, false},
                                           ContainingRow{"AnUnrelatedPointer", 9, 0, false},
                                           ContainingRow{"Null", 0, 0, false}),
                         [](const ::testing::TestParamInfo<ContainingRow>& info) { return std::string(info.param.name); });

TEST(Small_UsmRegistry, AnEntryWithoutALengthResolvesOnlyAtItsBase)
{
    // An entry without a length resolves only at its base; see README.md.
    USMRegistry registry;
    registry.record(fake(1), 7, std::nullopt, std::nullopt);

    const auto base = registry.containing_allocation(fake(1));
    ASSERT_TRUE(base.has_value());
    EXPECT_EQ(base->device_index, 7u);
    EXPECT_FALSE(registry.containing_allocation(inside(1, 8)).has_value());
}
