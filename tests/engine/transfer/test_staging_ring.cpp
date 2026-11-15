/**************************************************************************/
/* test_staging_ring.cpp                                                  */
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
 * @file test_staging_ring.cpp
 * @brief Tests that StagingRing makes its slots once, hands each to one holder at a time and frees them with the ring.
 * @see tests/engine/transfer/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <stdexcept>

#include <gtest/gtest.h>

#include <SushiRuntime/transfer/staging_ring.hpp>

#include "support/transfer_path_doubles.hpp"

using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Refuses every allocation. */
    class EmptyPinnedHostMemory final : public PinnedHostMemory
    {
        public:
            [[nodiscard]] void* allocate(std::size_t) override
            {
                return nullptr;
            }

            void release(void*) noexcept override
            {

            }
    };

    TEST(Small_StagingRing, MakesNoSlotBeforeTheFirstAcquire)
    {
        HeapPinnedHostMemory memory;
        const StagingRing ring(memory, StagingRingShape{16, 3});

        EXPECT_EQ(memory.allocated(), 0);
        EXPECT_EQ(ring.shape().slot_bytes, 16U);
        EXPECT_EQ(ring.shape().slot_count, 3U);
    }

    TEST(Small_StagingRing, RaisesAZeroShapeToOne)
    {
        HeapPinnedHostMemory memory;
        const StagingRing ring(memory, StagingRingShape{0, 0});

        EXPECT_EQ(ring.shape().slot_bytes, 1U);
        EXPECT_EQ(ring.shape().slot_count, 1U);
    }

    TEST(Small_StagingRing, MakesAllSlotsOnceAndHandsThemOutInRingOrder)
    {
        HeapPinnedHostMemory memory;
        StagingRing ring(memory, StagingRingShape{16, 3});

        const StagingSlot first = ring.acquire();
        const StagingSlot second = ring.acquire();
        const StagingSlot third = ring.acquire();
        EXPECT_EQ(memory.allocated(), 3);
        EXPECT_EQ(first.index, 0U);
        EXPECT_EQ(second.index, 1U);
        EXPECT_EQ(third.index, 2U);
        EXPECT_NE(first.data, second.data);
        EXPECT_NE(second.data, third.data);

        ring.retire(first, sycl::event{});
        ring.retire(second, sycl::event{});
        ring.retire(third, sycl::event{});
        const StagingSlot again = ring.acquire();
        EXPECT_EQ(again.index, 0U);
        EXPECT_EQ(again.data, first.data);
        EXPECT_EQ(memory.allocated(), 3);
        ring.retire(again, sycl::event{});
    }

    TEST(Small_StagingRing, SkipsASlotThatIsStillHeld)
    {
        HeapPinnedHostMemory memory;
        StagingRing ring(memory, StagingRingShape{16, 2});

        const StagingSlot held = ring.acquire();
        const StagingSlot other = ring.acquire();
        ring.retire(other, sycl::event{});
        const StagingSlot next = ring.acquire();

        EXPECT_EQ(next.index, other.index);
        ring.retire(next, sycl::event{});
        ring.retire(held, sycl::event{});
    }

    TEST(Small_StagingRing, FreesEverySlotWithTheRing)
    {
        HeapPinnedHostMemory memory;
        {
            StagingRing ring(memory, StagingRingShape{16, 4});
            ring.retire(ring.acquire(), sycl::event{});
        }

        EXPECT_EQ(memory.allocated(), 4);
        EXPECT_EQ(memory.released(), 4);
    }

    TEST(Small_StagingRing, RefusesWhenThePinnedMemoryHasNoRoom)
    {
        EmptyPinnedHostMemory memory;
        StagingRing ring(memory);

        EXPECT_THROW(static_cast<void>(ring.acquire()), std::runtime_error);
    }
} // namespace
