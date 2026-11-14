/**************************************************************************/
/* test_mpsc_ring.cpp                                                      */
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
 * @file test_mpsc_ring.cpp
 * @brief Tests MPSCRing: the bounded ring contract, and many producer threads handing values to one consumer.
 * @see tests/engine/work_stealing/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/work_stealing/mpsc_ring.hpp>

#include "support/bounded_ring_contract.hpp"

using SushiRuntime::Internal::Scheduler::MPSCRing;
using namespace TestSupport::Rings;

namespace TestSupport
{
    /** @brief The ring the contract rows run on. */
    using SmallMPSCRing = MPSCRing<std::uint32_t, K_SMALL_CAPACITY>;

    INSTANTIATE_TYPED_TEST_SUITE_P(Small_MPSCRing, BoundedRingContract, SmallMPSCRing, );
} // namespace TestSupport

/** @brief Tests that many producers' values all come out once, each producer's in its own order. */
TEST(Medium_MPSCRing, ManyProducersHandEveryValueOverOnceAndInTheirOrder)
{
    MPSCRing<std::uint32_t, K_SHARED_CAPACITY> ring;
    std::vector<std::thread> producers = start_producers(ring, K_PRODUCERS);

    const Consumed seen = consume_everything(ring, K_PRODUCERS);
    join_all(producers);

    EXPECT_TRUE(seen.in_order);
    EXPECT_EQ(seen.next_index, std::vector<std::uint32_t>(K_PRODUCERS, K_VALUES_PER_PRODUCER));
}
