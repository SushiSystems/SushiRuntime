/**************************************************************************/
/* test_late_count_resolution.cpp                                         */
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
 * @file test_late_count_resolution.cpp
 * @brief Tests late_count_source() and read_live_count(): the host path and an over-capacity count.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/api/detail/late_count_resolution.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that a fixed count is its own live value, and a host count calls its provider. */
TEST(Small_LateCountResolution, HostCountsResolveOnTheHost)
{
    const Detail::LiveCountSource fixed = Detail::late_count_source(LateCount::fixed(6), nullptr);
    EXPECT_EQ(Detail::read_live_count(fixed, nullptr), 6u);

    const Detail::LiveCountSource host =
        Detail::late_count_source(LateCount::from_host(10, [] { return std::size_t{3}; }), nullptr);
    EXPECT_EQ(Detail::read_live_count(host, nullptr), 3u);
}

/** @brief Tests that a host count above the capacity is recorded as a fault and resolves to zero. */
TEST(Small_LateCountResolution, AHostCountAboveTheCapacityResolvesToZero)
{
    std::uint32_t fault[Detail::K_DEVICE_FAULT_WORDS] = {};
    const Detail::LiveCountSource source =
        Detail::late_count_source(LateCount::from_host(10, [] { return std::size_t{11}; }), fault);
    EXPECT_EQ(Detail::read_live_count(source, nullptr), 0u);
    EXPECT_EQ(fault[0], Detail::K_DEVICE_FAULT_LATE_COUNT);
    EXPECT_EQ(fault[2], 11u);
    EXPECT_EQ(fault[3], 10u);
}
