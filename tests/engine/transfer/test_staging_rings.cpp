/**************************************************************************/
/* test_staging_rings.cpp                                                 */
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
 * @file test_staging_rings.cpp
 * @brief Tests that StagingRings keeps one ring per SYCL context, each made through its own memory.
 * @see tests/engine/transfer/README.md
 * @author Mustafa Garip
 */

#include <optional>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/transfer/staging_rings.hpp>

#include "support/transfer_path_doubles.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Holds two SYCL contexts on the CPU device. */
    class Medium_StagingRings : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                try
                {
                    device_.emplace(sycl::cpu_selector_v);
                }
                catch (const sycl::exception&)
                {
                    GTEST_SKIP() << "SYCL offers no CPU device.";
                }
                first_.emplace(*device_);
                second_.emplace(*device_);
            }

            std::optional<sycl::device> device_;
            std::optional<sycl::context> first_;
            std::optional<sycl::context> second_;
            HeapPinnedHostMemory first_memory_;
            HeapPinnedHostMemory second_memory_;
            StagingRings rings_{StagingRingShape{16, 2}};
    };

    TEST_F(Medium_StagingRings, EachContextGetsItsOwnRingMadeThroughItsOwnMemory)
    {
        rings_.add(*first_, first_memory_);
        rings_.add(*second_, second_memory_);

        StagingRing* const first = rings_.ring_of(*first_);
        StagingRing* const second = rings_.ring_of(*second_);
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        EXPECT_NE(first, second);
        const Result<StagingSlot> slot = second->acquire();
        ASSERT_TRUE(slot);
        second->retire(slot.value(), sycl::event{});
        EXPECT_EQ(first_memory_.allocated(), 0);
        EXPECT_EQ(second_memory_.allocated(), 2);
    }

    TEST_F(Medium_StagingRings, AContextAddedTwiceKeepsItsFirstRing)
    {
        rings_.add(*first_, first_memory_);
        StagingRing* const kept = rings_.ring_of(*first_);

        rings_.add(*first_, second_memory_);

        EXPECT_EQ(rings_.ring_of(*first_), kept);
    }

    TEST_F(Medium_StagingRings, AContextNeverAddedHasNoRing)
    {
        rings_.add(*first_, first_memory_);

        EXPECT_EQ(rings_.ring_of(*second_), nullptr);
    }
} // namespace
