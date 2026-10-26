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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_usm_allocator_zones.cpp
 * @brief Tests the zones and values the USM allocator writes for an allocation and a release.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>

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

/** @brief Tests that an allocation writes a memory.allocate zone and a value of its size. */
TEST(Medium_UsmAllocatorZones, AnAllocationWritesAZoneAndItsSize)
{
    auto runtime = Runtime::create().value();

    const RunRecord record = record_periods_without_source(
        0, 1, [&runtime](std::uint32_t, SushiProfiler::PeriodTimeline&)
        {
            auto buffer = runtime.buffer<float>(K_COUNT);
            EXPECT_TRUE(buffer.has_value());
        });

    ASSERT_EQ(record.periods.size(), 1u);
    const std::set<std::string> sites = zone_sites(record, record.periods[0]);
    EXPECT_TRUE(sites.count("memory.allocate"));
    EXPECT_TRUE(sites.count("memory.release")) << "the buffer was released at the end of the period's work";
    const auto bytes = sample_total(record, "memory.allocate_bytes");
    EXPECT_GE(bytes.first, static_cast<double>(K_COUNT * sizeof(float)));
    EXPECT_GE(bytes.second, 1u);
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

    std::size_t seen_64 = 0;
    std::size_t seen_4096 = 0;
    for (const SushiProfiler::SampleSeries& series : record.periods.at(0).samples)
        if (record.sites[series.site].name == "memory.allocate_bytes")
            for (const double value : series.values)
            {
                seen_64 += value == 64.0 ? 1 : 0;
                seen_4096 += value == 4096.0 ? 1 : 0;
            }
    EXPECT_EQ(seen_64, 1u);
    EXPECT_EQ(seen_4096, 1u);
}
