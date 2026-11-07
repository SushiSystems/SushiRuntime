/**************************************************************************/
/* test_worker_buffer_cache.cpp                                           */
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
 * @file test_worker_buffer_cache.cpp
 * @brief Tests WorkerBufferCache: pins, release, the byte budget, eviction order and chunk assembly.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/dispatch_executor/worker_buffer_cache.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "support/heap_cache_allocator.hpp"
#include "support/worker_wire.hpp"

using namespace SushiRuntime::Distributed;
using namespace TestSupport::WorkerWire;

namespace
{
    /** @brief Holds the budget of the cache every case builds: three 100-byte buffers. */
    constexpr std::uint64_t K_BUDGET = 300;

    /** @brief Waits for no chunk: a pull whose outcome is already set returns at once. */
    constexpr std::chrono::milliseconds K_NO_WAIT{0};

    /** @brief Builds a cache of K_BUDGET bytes on a heap allocator. */
    class Small_WorkerBufferCache : public ::testing::Test
    {
        protected:
            /** @brief Builds the cache on the fixture's allocator. */
            Small_WorkerBufferCache() : cache_(heap_.port(), K_BUDGET)
            {

            }

            /** @brief Makes an output of a handle valid in the cache and unpinned. */
            void store(const BufferHandle& handle)
            {
                BufferView view;
                ASSERT_EQ(cache_.pin_output(handle, view), CacheStatus::Ready);
                cache_.commit_output(handle);
                cache_.unpin(handle.id);
            }

            /** @brief Reads a cached input, unpins it and returns what begin_input() answered. */
            CacheStatus read(const BufferHandle& handle)
            {
                BufferView view;
                const CacheStatus status = cache_.begin_input(handle, view);
                if (status == CacheStatus::Ready)
                    cache_.unpin(handle.id);
                return status;
            }

            /// @brief Serves the cache's blocks and counts the live ones.
            TestSupport::HeapCacheAllocator heap_;

            /// @brief The cache under test.
            WorkerBufferCache cache_;
    };
} // namespace

/** @brief Tests that releasing an unpinned buffer frees its memory and counts one eviction. */
TEST_F(Small_WorkerBufferCache, ReleaseFreesAnUnpinnedBuffer)
{
    store(handle_of(1, 1, 64));
    ASSERT_EQ(cache_.bytes(), 64u);

    cache_.release(GlobalBufferID{1});

    EXPECT_EQ(cache_.bytes(), 0u);
    EXPECT_EQ(cache_.evictions(), 1u);
    EXPECT_EQ(heap_.live_blocks(), 0u);
}

/** @brief Tests that releasing a pinned buffer keeps its memory until the last pin goes. */
TEST_F(Small_WorkerBufferCache, ReleaseOfAPinnedBufferFreesItWhenTheLastPinGoes)
{
    const BufferHandle handle = handle_of(1, 1, 64);
    BufferView view;
    ASSERT_EQ(cache_.pin_output(handle, view), CacheStatus::Ready);
    cache_.commit_output(handle);

    cache_.release(handle.id);
    EXPECT_EQ(cache_.evictions(), 1u);
    EXPECT_EQ(cache_.bytes(), 64u) << "the memory of a pinned buffer was freed under its reader";

    cache_.unpin(handle.id);
    EXPECT_EQ(cache_.bytes(), 0u);
    EXPECT_EQ(heap_.live_blocks(), 0u);
}

/** @brief Tests that a full cache evicts the buffer used longest ago, and keeps the one read. */
TEST_F(Small_WorkerBufferCache, BudgetEvictsTheLeastRecentlyUsedUnpinnedBuffer)
{
    store(handle_of(1, 1, 100));
    store(handle_of(2, 1, 100));
    store(handle_of(3, 1, 100));
    ASSERT_EQ(read(handle_of(1, 1, 100)), CacheStatus::Ready);

    store(handle_of(4, 1, 100));

    EXPECT_EQ(cache_.evictions(), 1u);
    EXPECT_EQ(cache_.bytes(), K_BUDGET);
    EXPECT_EQ(read(handle_of(1, 1, 100)), CacheStatus::Ready) << "the buffer used last was evicted";
    EXPECT_EQ(read(handle_of(2, 1, 100)), CacheStatus::NeedsPull)
        << "the buffer used longest ago stayed";
}

/** @brief Tests that a pinned buffer is never evicted to make room, and the last output fails. */
TEST_F(Small_WorkerBufferCache, PinnedBuffersAreNotEvictedToMakeRoom)
{
    store(handle_of(1, 1, 100));
    BufferView pinned_output;
    ASSERT_EQ(cache_.pin_output(handle_of(3, 1, 100), pinned_output), CacheStatus::Ready);
    BufferView pulled_input;
    ASSERT_EQ(cache_.begin_input(handle_of(2, 1, 100), pulled_input), CacheStatus::NeedsPull);
    BufferView evicting_output;
    ASSERT_EQ(cache_.pin_output(handle_of(4, 1, 100), evicting_output), CacheStatus::Ready);
    ASSERT_EQ(cache_.evictions(), 1u);

    BufferView refused;
    EXPECT_EQ(cache_.pin_output(handle_of(5, 1, 100), refused), CacheStatus::AllocationFailed);
    EXPECT_EQ(cache_.evictions(), 1u) << "a pinned buffer was evicted to make room";
    EXPECT_EQ(cache_.bytes(), K_BUDGET);
}

/** @brief Tests that an output larger than the whole budget fails and holds nothing. */
TEST_F(Small_WorkerBufferCache, AnOutputPastTheBudgetFailsAndHoldsNothing)
{
    BufferView view;

    EXPECT_EQ(cache_.pin_output(handle_of(1, 1, K_BUDGET + 1), view),
              CacheStatus::AllocationFailed);
    EXPECT_EQ(cache_.bytes(), 0u);
}

/** @brief Tests that an allocator with no room fails the output and holds nothing for it. */
TEST(Small_WorkerBufferCacheAllocator, AnAllocatorWithNoRoomFailsTheOutput)
{
    TestSupport::HeapCacheAllocator heap(100);
    WorkerBufferCache cache(heap.port(), K_BUDGET);
    BufferView first;
    ASSERT_EQ(cache.pin_output(handle_of(1, 1, 100), first), CacheStatus::Ready);

    BufferView second;
    EXPECT_EQ(cache.pin_output(handle_of(2, 1, 100), second), CacheStatus::AllocationFailed);
    EXPECT_EQ(cache.bytes(), 100u);
    EXPECT_EQ(heap.live_blocks(), 1u);
}

/** @brief Tests that an output of another size than the cached one replaces the old buffer. */
TEST_F(Small_WorkerBufferCache, AnOutputOfAnotherSizeReplacesTheCachedBuffer)
{
    store(handle_of(1, 1, 64));

    BufferView view;
    ASSERT_EQ(cache_.pin_output(handle_of(1, 2, 32), view), CacheStatus::Ready);

    EXPECT_EQ(view.size_bytes, 32u);
    EXPECT_EQ(cache_.bytes(), 32u);
    EXPECT_EQ(heap_.live_blocks(), 1u);
}

/** @brief Tests that a pulled buffer with a gap is discarded and keeps no memory. */
TEST_F(Small_WorkerBufferCache, AGapInAPulledBufferDiscardsIt)
{
    const BufferHandle handle = handle_of(9, 3, 12);
    const std::vector<std::byte> bytes = counting_bytes(12, 40);
    BufferView view;
    ASSERT_EQ(cache_.begin_input(handle, view), CacheStatus::NeedsPull);

    cache_.accept_chunk(pull_chunk(handle, 0, 4, bytes), false);
    cache_.accept_chunk(pull_chunk(handle, 8, 4, bytes), true);

    EXPECT_EQ(cache_.wait_input(handle, K_NO_WAIT, view), CacheStatus::Aborted);
    EXPECT_EQ(cache_.bytes(), 0u) << "a discarded transfer kept its memory";
}

/** @brief Tests that a pull whose chunks follow each other is kept, byte for byte. */
TEST_F(Small_WorkerBufferCache, AWholePullIsKeptWithTheBytesTheMasterSent)
{
    const BufferHandle handle = handle_of(9, 3, 12);
    const std::vector<std::byte> bytes = counting_bytes(12, 40);
    BufferView view;
    ASSERT_EQ(cache_.begin_input(handle, view), CacheStatus::NeedsPull);

    cache_.accept_chunk(pull_chunk(handle, 0, 4, bytes), false);
    cache_.accept_chunk(pull_chunk(handle, 4, 4, bytes), false);
    cache_.accept_chunk(pull_chunk(handle, 8, 4, bytes), true);

    ASSERT_EQ(cache_.wait_input(handle, K_NO_WAIT, view), CacheStatus::Ready);
    ASSERT_EQ(view.size_bytes, bytes.size());
    EXPECT_EQ(std::memcmp(view.data, bytes.data(), bytes.size()), 0);
    EXPECT_EQ(cache_.bytes(), 12u);
}

/** @brief Tests that a BUFFER_ABORT ends the pull it names. */
TEST_F(Small_WorkerBufferCache, AnAbortEndsTheOpenPull)
{
    const BufferHandle handle = handle_of(9, 3, 12);
    BufferView view;
    ASSERT_EQ(cache_.begin_input(handle, view), CacheStatus::NeedsPull);

    cache_.abort_transfer(handle.id, handle.version);

    EXPECT_EQ(cache_.wait_input(handle, K_NO_WAIT, view), CacheStatus::Aborted);
    EXPECT_EQ(cache_.bytes(), 0u);
}

/** @brief Tests that clear() cancels the open pull and frees its memory. */
TEST_F(Small_WorkerBufferCache, ClearCancelsTheOpenPull)
{
    const BufferHandle handle = handle_of(9, 3, 12);
    BufferView view;
    ASSERT_EQ(cache_.begin_input(handle, view), CacheStatus::NeedsPull);

    cache_.clear();

    EXPECT_EQ(cache_.wait_input(handle, K_NO_WAIT, view), CacheStatus::Cancelled);
    EXPECT_EQ(cache_.bytes(), 0u);
    EXPECT_EQ(heap_.live_blocks(), 0u);
}

/** @brief Tests that a buffer already pulled for one reader needs no second pull for the next. */
TEST_F(Small_WorkerBufferCache, ASecondReadOfAPulledBufferNeedsNoPull)
{
    const BufferHandle handle = handle_of(3, 1, 8);
    const std::vector<std::byte> bytes = counting_bytes(8, 1);
    BufferView first;
    ASSERT_EQ(cache_.begin_input(handle, first), CacheStatus::NeedsPull);
    cache_.accept_chunk(pull_chunk(handle, 0, 8, bytes), true);
    ASSERT_EQ(cache_.wait_input(handle, K_NO_WAIT, first), CacheStatus::Ready);

    BufferView second;
    EXPECT_EQ(cache_.begin_input(handle, second), CacheStatus::Ready);
    EXPECT_EQ(second.data, first.data);
}
