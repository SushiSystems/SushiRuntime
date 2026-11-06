/**************************************************************************/
/* test_usm_allocator_zones.cpp                                           */
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
 * @file test_usm_allocator_zones.cpp
 * @brief Tests the zones and values the USM allocator writes for an allocation and a release.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "session_pipeline.hpp"

using namespace SushiRuntime;
using namespace SessionTests;
using SushiProfiler::RunRecord;

namespace
{
    constexpr std::size_t K_COUNT = 1000;
} // namespace

namespace
{
    /** @brief Records one period around the allocation of a buffer of @p count floats. */
    RunRecord record_one_allocation(Runtime& runtime, std::size_t count)
    {
        return record_periods_without_source(
            0, 1, [&runtime, count](std::uint32_t, SushiProfiler::PeriodTimeline&)
            {
                auto buffer = runtime.buffer<float>(count);
                EXPECT_TRUE(buffer.has_value());
            });
    }

    /** @brief Returns how many samples of site @p name in the first period of @p record equal @p value. */
    std::size_t samples_equal_to(const RunRecord& record, const char* name, double value)
    {
        std::size_t equal = 0;
        for (const SushiProfiler::SampleSeries& series : record.periods.at(0).samples)
            if (record.sites[series.site].name == name)
                for (const double sample : series.values)
                    equal += sample == value ? 1 : 0;
        return equal;
    }
} // namespace

/** @brief Tests that an allocation writes a memory.allocate zone and a value of its size. */
TEST(Medium_UsmAllocatorZones, AnAllocationWritesAZoneAndItsSize)
{
    auto runtime = Runtime::create().value();

    const RunRecord record = record_one_allocation(runtime, K_COUNT);

    ASSERT_EQ(record.periods.size(), 1u);
    EXPECT_TRUE(zone_sites(record, record.periods[0]).count("memory.allocate"));
    const auto bytes = sample_total(record, "memory.allocate_bytes");
    EXPECT_GE(bytes.first, static_cast<double>(K_COUNT * sizeof(float)));
    EXPECT_GE(bytes.second, 1u);
}

/** @brief Tests that releasing the buffer writes a memory.release zone. */
TEST(Medium_UsmAllocatorZones, AReleaseWritesAZone)
{
    auto runtime = Runtime::create().value();

    const RunRecord record = record_one_allocation(runtime, K_COUNT);

    ASSERT_EQ(record.periods.size(), 1u);
    EXPECT_TRUE(zone_sites(record, record.periods[0]).count("memory.release"))
        << "the buffer was released at the end of the period's work";
}

/** @brief Tests that the value of an allocation is the size of that allocation, one value per allocation. */
TEST(Medium_UsmAllocatorZones, EachAllocationWritesOneValueOfItsOwnSize)
{
    auto runtime = Runtime::create().value();

    const RunRecord record = record_periods_without_source(
        0, 1, [&runtime](std::uint32_t, SushiProfiler::PeriodTimeline&)
        {
            auto first = runtime.buffer<std::uint8_t>(64);
            auto second = runtime.buffer<std::uint8_t>(4096);
            EXPECT_TRUE(first.has_value());
            EXPECT_TRUE(second.has_value());
        });

    EXPECT_EQ(samples_equal_to(record, "memory.allocate_bytes", 64.0), 1u);
    EXPECT_EQ(samples_equal_to(record, "memory.allocate_bytes", 4096.0), 1u);
}
