/**************************************************************************/
/* bounded_ring_contract.hpp                                               */
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
 * @file bounded_ring_contract.hpp
 * @brief Declares BoundedRingContract, the typed suite every bounded ring passes, and the helpers its threaded tests share.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace TestSupport
{
    namespace Rings
    {
        /** @brief The capacity of the rings the small tests build. */
        inline constexpr std::size_t K_SMALL_CAPACITY = 4;

        /** @brief The laps the wrap test fills and drains a ring through. */
        inline constexpr std::size_t K_LAPS = 10;

        /**
         * @brief Pushes the values @p first to @p first + @p count - 1, in order, and returns how many went in.
         * @tparam Ring An MPSCRing or SPSCRing of std::uint32_t.
         */
        template <class Ring>
        std::size_t push_run(Ring& ring, std::uint32_t first, std::size_t count)
        {
            std::size_t pushed = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                std::uint32_t value = first + static_cast<std::uint32_t>(i);
                if (ring.try_push(value))
                    ++pushed;
            }
            return pushed;
        }

        /**
         * @brief Pops @p ring empty and returns the values in the order they came out.
         * @tparam Ring An MPSCRing or SPSCRing of std::uint32_t.
         */
        template <class Ring>
        std::vector<std::uint32_t> pop_all(Ring& ring)
        {
            std::vector<std::uint32_t> out;
            std::uint32_t value = 0;
            while (ring.try_pop(value))
                out.push_back(value);
            return out;
        }

        /** @brief Returns the values @p first to @p first + @p count - 1. */
        inline std::vector<std::uint32_t> run_of(std::uint32_t first, std::size_t count)
        {
            std::vector<std::uint32_t> values;
            for (std::size_t i = 0; i < count; ++i)
                values.push_back(first + static_cast<std::uint32_t>(i));
            return values;
        }

        /**
         * @brief Fills @p ring and pops it empty @p laps times, counting up from 0, and returns every value popped.
         * @tparam Ring An MPSCRing or SPSCRing of std::uint32_t.
         */
        template <class Ring>
        std::vector<std::uint32_t> fill_and_drain_laps(Ring& ring, std::size_t laps)
        {
            std::vector<std::uint32_t> out;
            for (std::size_t lap = 0; lap < laps; ++lap)
            {
                push_run(ring, static_cast<std::uint32_t>(lap * Ring::K_CAPACITY), Ring::K_CAPACITY);
                const std::vector<std::uint32_t> drained = pop_all(ring);
                out.insert(out.end(), drained.begin(), drained.end());
            }
            return out;
        }

        /** @brief The threads that push at once in the many-producer test. */
        inline constexpr std::size_t K_PRODUCERS = 4;

        /** @brief The values each producer pushes. */
        inline constexpr std::uint32_t K_VALUES_PER_PRODUCER = 20000;

        /** @brief The capacity of the rings the medium tests share; far below what is pushed, so they fill and wrap. */
        inline constexpr std::size_t K_SHARED_CAPACITY = 64;

        /** @brief Encodes producer @p producer's @p index-th value, so the consumer can tell producers apart. */
        constexpr std::uint32_t encode(std::size_t producer, std::uint32_t index) noexcept
        {
            return static_cast<std::uint32_t>(producer) * K_VALUES_PER_PRODUCER + index;
        }

        /**
         * @brief Pushes producer @p producer's values in order, retrying each until the ring takes it.
         * @tparam Ring An MPSCRing or SPSCRing of std::uint32_t.
         */
        template <class Ring>
        void produce(Ring& ring, std::size_t producer)
        {
            for (std::uint32_t index = 0; index < K_VALUES_PER_PRODUCER; ++index)
            {
                std::uint32_t value = encode(producer, index);
                while (!ring.try_push(value))
                    std::this_thread::yield();
            }
        }

        /** @brief Starts @p producers threads, each running produce() on @p ring with its own producer number. */
        template <class Ring>
        std::vector<std::thread> start_producers(Ring& ring, std::size_t producers)
        {
            std::vector<std::thread> threads;
            for (std::size_t producer = 0; producer < producers; ++producer)
                threads.emplace_back(produce<Ring>, std::ref(ring), producer);
            return threads;
        }

        /** @brief Joins every thread of @p threads. */
        inline void join_all(std::vector<std::thread>& threads)
        {
            for (std::thread& thread : threads)
                thread.join();
        }

        /** @brief What the consumer saw: per producer, the next index it expects and whether every value came in order. */
        struct Consumed
        {
            std::vector<std::uint32_t> next_index; /**< Per producer: how many of its values came out in order. */
            bool in_order = true;                  /**< No value was skipped, repeated or foreign. */
        };

        /**
         * @brief Pops until @p producers producers' values have all come out, checking each producer's order.
         * @tparam Ring An MPSCRing or SPSCRing of std::uint32_t.
         */
        template <class Ring>
        Consumed consume_everything(Ring& ring, std::size_t producers)
        {
            Consumed seen;
            seen.next_index.assign(producers, 0);
            std::size_t remaining = producers * K_VALUES_PER_PRODUCER;
            std::uint32_t value = 0;
            while (remaining != 0)
            {
                if (!ring.try_pop(value))
                {
                    std::this_thread::yield();
                    continue;
                }
                const std::size_t producer = value / K_VALUES_PER_PRODUCER;
                const std::uint32_t index = value % K_VALUES_PER_PRODUCER;
                if (producer >= producers || index != seen.next_index[producer])
                    seen.in_order = false;
                else
                    ++seen.next_index[producer];
                --remaining;
            }
            return seen;
        }
    } // namespace Rings

    /**
     * @brief Fixture of the single-thread promises every bounded ring keeps.
     * @tparam Ring A ring of std::uint32_t of Rings::K_SMALL_CAPACITY with try_push and try_pop.
     */
    template <class Ring>
    class BoundedRingContract : public ::testing::Test
    {
        protected:
            /** @brief The ring under test, empty. */
            Ring ring_;
    };

    TYPED_TEST_SUITE_P(BoundedRingContract);

    /** @brief Tests that one producer's values come out in the order they went in. */
    TYPED_TEST_P(BoundedRingContract, OneProducersValuesComeOutInOrder)
    {
        Rings::push_run(this->ring_, 10, 3);

        EXPECT_EQ(Rings::pop_all(this->ring_), Rings::run_of(10, 3));
    }

    /** @brief Tests that an empty ring pops nothing and leaves the output untouched. */
    TYPED_TEST_P(BoundedRingContract, AnEmptyRingPopsNothing)
    {
        std::uint32_t out = 77;

        EXPECT_FALSE(this->ring_.try_pop(out));
        EXPECT_EQ(out, 77u);
    }

    /** @brief Tests that a full ring refuses a push, leaves the value with the caller and keeps what it holds. */
    TYPED_TEST_P(BoundedRingContract, AFullRingRefusesAPushAndKeepsItsValues)
    {
        ASSERT_EQ(Rings::push_run(this->ring_, 1, Rings::K_SMALL_CAPACITY), Rings::K_SMALL_CAPACITY);
        std::uint32_t refused = 99;

        EXPECT_FALSE(this->ring_.try_push(refused));
        EXPECT_EQ(refused, 99u);
        EXPECT_EQ(Rings::pop_all(this->ring_), Rings::run_of(1, Rings::K_SMALL_CAPACITY));
    }

    /** @brief Tests that a pop frees a slot of a full ring for the next push. */
    TYPED_TEST_P(BoundedRingContract, APopFreesASlotOfAFullRing)
    {
        ASSERT_EQ(Rings::push_run(this->ring_, 1, Rings::K_SMALL_CAPACITY), Rings::K_SMALL_CAPACITY);
        std::uint32_t first = 0;
        ASSERT_TRUE(this->ring_.try_pop(first));
        std::uint32_t next = 5;

        EXPECT_TRUE(this->ring_.try_push(next));
        EXPECT_EQ(Rings::pop_all(this->ring_), Rings::run_of(2, Rings::K_SMALL_CAPACITY));
    }

    /** @brief Tests that values keep their order across many wraps of the slot index. */
    TYPED_TEST_P(BoundedRingContract, ValuesKeepTheirOrderAcrossWraps)
    {
        const std::size_t total = Rings::K_LAPS * Rings::K_SMALL_CAPACITY;

        EXPECT_EQ(Rings::fill_and_drain_laps(this->ring_, Rings::K_LAPS), Rings::run_of(0, total));
    }

    REGISTER_TYPED_TEST_SUITE_P(BoundedRingContract, OneProducersValuesComeOutInOrder,
                                AnEmptyRingPopsNothing, AFullRingRefusesAPushAndKeepsItsValues,
                                APopFreesASlotOfAFullRing, ValuesKeepTheirOrderAcrossWraps);

} // namespace TestSupport
