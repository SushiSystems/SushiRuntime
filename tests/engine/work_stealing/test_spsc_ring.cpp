/**************************************************************************/
/* test_spsc_ring.cpp                                                      */
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
 * @file test_spsc_ring.cpp
 * @brief Tests SPSCRing: the bounded ring contract, and one producer thread handing values to one consumer.
 * @see tests/engine/work_stealing/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/work_stealing/spsc_ring.hpp>

#include "support/bounded_ring_contract.hpp"

using SushiRuntime::Internal::Scheduler::SPSCRing;
using namespace TestSupport::Rings;

namespace TestSupport
{
    /** @brief The ring the contract rows run on. */
    using SmallSPSCRing = SPSCRing<std::uint32_t, K_SMALL_CAPACITY>;

    INSTANTIATE_TYPED_TEST_SUITE_P(Small_SPSCRing, BoundedRingContract, SmallSPSCRing, );
} // namespace TestSupport

/** @brief Tests that a producer thread hands every value to a consumer thread once and in order. */
TEST(Medium_SPSCRing, AProducerThreadHandsEveryValueOverOnceAndInOrder)
{
    SPSCRing<std::uint32_t, K_SHARED_CAPACITY> ring;
    std::vector<std::thread> producer = start_producers(ring, 1);

    const Consumed seen = consume_everything(ring, 1);
    join_all(producer);

    EXPECT_TRUE(seen.in_order);
    EXPECT_EQ(seen.next_index, std::vector<std::uint32_t>(1, K_VALUES_PER_PRODUCER));
}
