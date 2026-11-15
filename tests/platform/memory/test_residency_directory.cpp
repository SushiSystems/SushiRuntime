/**************************************************************************/
/* test_residency_directory.cpp                                           */
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
 * @file test_residency_directory.cpp
 * @brief Tests minting, resolving and retiring handles in the residency directory.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Memory;

namespace
{
    constexpr std::uint32_t K_UNIVERSE = 7;

    /** @brief Returns a handle minted in directory, failing the test when none is minted. */
    GlobalHandle mint_buffer(ResidencyDirectory& directory)
    {
        const MintOutcome outcome = directory.mint(HandleKind::Buffer, HandleFlags::None);
        EXPECT_EQ(outcome.status, HandleStatus::Ok);
        return outcome.handle;
    }

    /** @brief Space 0 plays the host and space 1 the device in the write and read pattern. */
    const MemorySpaceID K_HOST_SPACE(0);
    const MemorySpaceID K_DEVICE_SPACE(1);
    constexpr std::uint64_t K_HOST_MASK = 1U;
    constexpr std::uint64_t K_DEVICE_MASK = 2U;

    /** @brief Steps of the pattern; step s writes byte s, so the buffer holds one byte more. */
    constexpr std::size_t K_STEPS = 100000;
    constexpr std::size_t K_BUFFER_BYTES = K_STEPS + 1;

    /** @brief A horizon falls after every K_HORIZON_EVERY steps. */
    constexpr std::size_t K_HORIZON_EVERY = 100;

    /** @brief Writes one byte on writer, then copies it to reader, as one step does. */
    void write_then_read(CoherenceIntervals& map, std::size_t byte, MemorySpaceID writer,
                         MemorySpaceID reader)
    {
        map.record_write(byte, byte + 1, writer);
        map.record_copy(byte, byte + 1, reader);
    }

    /**
     * @brief Runs steps [first, last) of the two-buffer pattern, retiring history at each horizon
     *        when horizons is set.
     * @return The interval count the last horizon reached, or 0 when no horizon fell in the range.
     */
    std::size_t run_steps(ResidencyDirectory& directory, GlobalHandle first_buffer,
                          GlobalHandle second_buffer, std::size_t first, std::size_t last,
                          bool horizons)
    {
        std::size_t intervals = 0;
        for (std::size_t step = first; step < last; ++step)
        {
            write_then_read(*directory.coherence(first_buffer), step, K_HOST_SPACE, K_DEVICE_SPACE);
            write_then_read(*directory.coherence(second_buffer), step, K_DEVICE_SPACE,
                            K_HOST_SPACE);
            if (horizons && (step + 1) % K_HORIZON_EVERY == 0)
                intervals = directory.retire_coherence_history();
        }
        return intervals;
    }

    TEST(Small_ResidencyDirectory, MintResolveAndRetire)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle handle = mint_buffer(directory);
        EXPECT_FALSE(handle.is_null());
        EXPECT_EQ(handle.universe(), K_UNIVERSE);
        EXPECT_EQ(handle.slot(), 0U);
        EXPECT_EQ(handle.generation(), 1U);

        int storage = 0;
        const MemorySpaceID space(3);
        EXPECT_EQ(directory.resolve(handle, space).status, HandleStatus::NotResident);
        EXPECT_EQ(directory.set_instance(handle, space, &storage), HandleStatus::Ok);
        EXPECT_EQ(directory.describe(handle, 64, ElementLayout{4, 4}, space), HandleStatus::Ok);

        const ResolveOutcome resolved = directory.resolve(handle, space);
        EXPECT_EQ(resolved.status, HandleStatus::Ok);
        EXPECT_EQ(resolved.address, &storage);

        const EntryView view = directory.inspect(handle);
        EXPECT_EQ(view.status, HandleStatus::Ok);
        EXPECT_EQ(view.size, 64U);
        EXPECT_EQ(view.layout.alignment, 4U);
        EXPECT_EQ(view.home, space);
        EXPECT_EQ(view.valid_spaces, std::uint64_t{1} << 3);

        EXPECT_EQ(directory.retire(handle), HandleStatus::Ok);
        EXPECT_TRUE(directory.is_stale(handle));
    }

    TEST(Small_ResidencyDirectory, AnInstanceKeepsItsKindUntilItIsDroppedOrRetired)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle handle = mint_buffer(directory);
        int device_storage = 0;
        int borrowed_storage = 0;
        const MemorySpaceID home(2);
        const MemorySpaceID other(5);
        ASSERT_EQ(directory.describe(handle, 4, ElementLayout{4, 4}, home), HandleStatus::Ok);
        ASSERT_EQ(directory.set_instance(handle, home, &device_storage, USMKind::Device),
                  HandleStatus::Ok);
        ASSERT_EQ(directory.set_instance(handle, other, &borrowed_storage), HandleStatus::Ok);

        EXPECT_EQ(directory.inspect(handle).home_kind, std::optional<USMKind>(USMKind::Device));
        EXPECT_EQ(directory.instance_kind(handle, home), std::optional<USMKind>(USMKind::Device));
        EXPECT_FALSE(directory.instance_kind(handle, other).has_value());

        ASSERT_EQ(directory.drop_instance(handle, home), HandleStatus::Ok);
        EXPECT_FALSE(directory.instance_kind(handle, home).has_value());
        EXPECT_FALSE(directory.inspect(handle).home_kind.has_value());

        ASSERT_EQ(directory.set_instance(handle, home, &device_storage, USMKind::Host),
                  HandleStatus::Ok);
        ASSERT_EQ(directory.retire(handle), HandleStatus::Ok);
        EXPECT_FALSE(directory.instance_kind(handle, home).has_value());
        const GlobalHandle reused = mint_buffer(directory);
        ASSERT_EQ(reused.slot(), handle.slot());
        ASSERT_EQ(directory.set_instance(reused, home, &device_storage), HandleStatus::Ok);
        EXPECT_FALSE(directory.instance_kind(reused, home).has_value());
    }

    TEST(Small_ResidencyDirectory, LiveCountFollowsMintAndRetire)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        EXPECT_EQ(directory.live_count(), 0U);
        const GlobalHandle first = mint_buffer(directory);
        const GlobalHandle second = mint_buffer(directory);
        EXPECT_EQ(directory.live_count(), 2U);

        ASSERT_EQ(directory.retire(first), HandleStatus::Ok);
        EXPECT_EQ(directory.retire(first), HandleStatus::StaleHandle);
        EXPECT_EQ(directory.live_count(), 1U);
        ASSERT_EQ(directory.retire(second), HandleStatus::Ok);
        EXPECT_EQ(directory.live_count(), 0U);
    }

    TEST(Small_ResidencyDirectory, AStaleHandleReturnsStaleHandle)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle handle = mint_buffer(directory);
        int storage = 0;
        EXPECT_EQ(directory.set_instance(handle, MemorySpaceID(0), &storage), HandleStatus::Ok);
        EXPECT_EQ(directory.retire(handle), HandleStatus::Ok);

        const ResolveOutcome outcome = directory.resolve(handle, MemorySpaceID(0));
        EXPECT_EQ(outcome.status, HandleStatus::StaleHandle);
        EXPECT_EQ(outcome.address, nullptr);
        EXPECT_EQ(directory.retire(handle), HandleStatus::StaleHandle);

        const Result<void*> result = to_result(outcome);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, ErrorCode::StaleHandle);
        EXPECT_EQ(error_code_of(HandleStatus::StaleHandle), ErrorCode::StaleHandle);
    }

    TEST(Small_ResidencyDirectory, NullAndForeignHandlesAreRefused)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle handle = mint_buffer(directory);
        const GlobalHandle foreign(K_UNIVERSE + 1, 0, HandleKind::Buffer, HandleFlags::None,
                                   handle.generation(), handle.slot());

        EXPECT_EQ(directory.resolve(GlobalHandle::null(), MemorySpaceID(0)).status,
                  HandleStatus::NullHandle);
        EXPECT_EQ(directory.resolve(foreign, MemorySpaceID(0)).status,
                  HandleStatus::ForeignUniverse);
        EXPECT_EQ(directory.retire(foreign), HandleStatus::ForeignUniverse);
    }

    TEST(Small_ResidencyDirectory, AFreedSlotIsReusedLowestFirstUnderTheNextGeneration)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        std::vector<GlobalHandle> handles;
        for (int i = 0; i < 6; ++i)
            handles.push_back(mint_buffer(directory));

        EXPECT_EQ(directory.retire(handles[5]), HandleStatus::Ok);
        EXPECT_EQ(directory.retire(handles[3]), HandleStatus::Ok);

        const GlobalHandle reused = mint_buffer(directory);
        EXPECT_EQ(reused.slot(), 3U);
        EXPECT_EQ(reused.generation(), handles[3].generation() + 1);
        EXPECT_TRUE(directory.is_stale(handles[3]));
        EXPECT_FALSE(directory.is_stale(reused));

        EXPECT_EQ(mint_buffer(directory).slot(), 5U);
        EXPECT_EQ(mint_buffer(directory).slot(), 6U);
    }

    TEST(Small_ResidencyDirectory, GrowingPastOneChunkKeepsEarlierEntriesInPlace)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle first = mint_buffer(directory);
        const void* const first_entry = directory.entry_address(first.slot());
        ASSERT_NE(first_entry, nullptr);
        EXPECT_EQ(directory.entry_address(ResidencyDirectory::K_CHUNK_ENTRIES), nullptr);

        int storage = 0;
        EXPECT_EQ(directory.set_instance(first, MemorySpaceID(1), &storage), HandleStatus::Ok);
        for (std::uint32_t i = 1; i <= ResidencyDirectory::K_CHUNK_ENTRIES; ++i)
            (void)mint_buffer(directory);

        EXPECT_NE(directory.entry_address(ResidencyDirectory::K_CHUNK_ENTRIES), nullptr);
        EXPECT_EQ(directory.entry_address(first.slot()), first_entry);
        EXPECT_EQ(directory.resolve(first, MemorySpaceID(1)).address, &storage);
    }

    TEST(Medium_ResidencyDirectory, ReadersResolveWhileAWriterMints)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        int storage = 0;
        const GlobalHandle stable = mint_buffer(directory);
        ASSERT_EQ(directory.set_instance(stable, MemorySpaceID(0), &storage), HandleStatus::Ok);

        std::atomic<bool> stop{false};
        std::atomic<int> wrong{0};
        std::vector<std::thread> readers;
        for (int i = 0; i < 4; ++i)
        {
            readers.emplace_back([&]() {
                while (!stop.load(std::memory_order_acquire))
                {
                    const ResolveOutcome outcome = directory.resolve(stable, MemorySpaceID(0));
                    if (outcome.status != HandleStatus::Ok || outcome.address != &storage)
                        wrong.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (std::uint32_t i = 0; i < 4 * ResidencyDirectory::K_CHUNK_ENTRIES; ++i)
        {
            const GlobalHandle handle = mint_buffer(directory);
            if (i % 2 == 0)
                EXPECT_EQ(directory.retire(handle), HandleStatus::Ok);
        }
        stop.store(true, std::memory_order_release);
        for (std::thread& reader : readers)
            reader.join();

        EXPECT_EQ(wrong.load(), 0);
    }

    TEST(Small_ResidencyDirectory, CoherenceMapLivesWithTheHandle)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle handle = mint_buffer(directory);
        CoherenceIntervals* const map = directory.coherence(handle);
        ASSERT_NE(map, nullptr);
        map->reset(64, MemorySpaceID(0));
        EXPECT_EQ(directory.coherence(handle)->size(), 64U);

        EXPECT_EQ(directory.retire(handle), HandleStatus::Ok);
        EXPECT_EQ(directory.coherence(handle), nullptr);
        const GlobalHandle reused = mint_buffer(directory);
        ASSERT_NE(directory.coherence(reused), nullptr);
        EXPECT_TRUE(directory.coherence(reused)->intervals().empty());
    }

    TEST(Small_ResidencyDirectory, WithoutHorizonsEveryStepAddsAnInterval)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle first = mint_buffer(directory);
        const GlobalHandle second = mint_buffer(directory);
        directory.coherence(first)->reset(K_BUFFER_BYTES, K_HOST_SPACE);
        directory.coherence(second)->reset(K_BUFFER_BYTES, K_HOST_SPACE);

        run_steps(directory, first, second, 0, 1000, false);
        // Each byte carries its own version, so no two written neighbours join.
        EXPECT_GT(directory.coherence(first)->intervals().size(), 1000U);
    }

    TEST(Medium_ResidencyDirectory, CoherenceHistoryStaysFlatOverOneHundredThousandSteps)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle first = mint_buffer(directory);
        const GlobalHandle second = mint_buffer(directory);
        directory.coherence(first)->reset(K_BUFFER_BYTES, K_HOST_SPACE);
        directory.coherence(second)->reset(K_BUFFER_BYTES, K_HOST_SPACE);

        const std::size_t after_one_thousand = run_steps(directory, first, second, 0, 1000, true);
        const std::size_t after_one_hundred_thousand =
            run_steps(directory, first, second, 1000, K_STEPS, true);

        EXPECT_EQ(after_one_thousand, after_one_hundred_thousand);
        // Each buffer is one written prefix on host and device, then one untouched host tail.
        EXPECT_EQ(after_one_hundred_thousand, 4U);
        EXPECT_EQ(directory.coherence(first)->valid_spaces_at(K_STEPS - 1),
                  K_HOST_MASK | K_DEVICE_MASK);
        EXPECT_EQ(directory.coherence(first)->valid_spaces_at(K_STEPS), K_HOST_MASK);
        EXPECT_EQ(directory.coherence(second)->valid_spaces_at(K_STEPS - 1),
                  K_HOST_MASK | K_DEVICE_MASK);
        EXPECT_EQ(directory.coherence(second)->valid_spaces_at(K_STEPS), K_HOST_MASK);
    }

    TEST(Small_ResidencyDirectory, AHostWriteMarksOnlyTheWrittenHandleAtTheNewGeneration)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle written = mint_buffer(directory);
        const GlobalHandle untouched = mint_buffer(directory);

        ASSERT_EQ(directory.record_host_write(written), HandleStatus::Ok);

        EXPECT_EQ(directory.host_write_generation(), 1U);
        EXPECT_EQ(directory.host_write_of(written), 1U);
        EXPECT_EQ(directory.host_write_of(untouched), 0U);
    }

    TEST(Small_ResidencyDirectory, ARetiredHandlesHostWriteIsNotSeenOnItsReusedSlot)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle retired = mint_buffer(directory);
        ASSERT_EQ(directory.record_host_write(retired), HandleStatus::Ok);
        ASSERT_EQ(directory.retire(retired), HandleStatus::Ok);

        const GlobalHandle reused = mint_buffer(directory);

        ASSERT_EQ(reused.slot(), retired.slot());
        EXPECT_EQ(directory.host_write_of(reused), 0U);
        EXPECT_EQ(directory.host_write_of(retired), 0U);
    }

    TEST(Small_ResidencyDirectory, AHostWriteOnARetiredHandleIsRefusedAndRaisesNoGeneration)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle retired = mint_buffer(directory);
        ASSERT_EQ(directory.retire(retired), HandleStatus::Ok);

        EXPECT_EQ(directory.record_host_write(retired), HandleStatus::StaleHandle);
        EXPECT_EQ(directory.host_write_generation(), 0U);
    }
} // namespace
