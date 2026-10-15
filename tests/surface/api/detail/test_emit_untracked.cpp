/**************************************************************************/
/* test_emit_untracked.cpp                                                */
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
 * @file test_emit_untracked.cpp
 * @brief Tests the untracked emitters: no regions, the device stated, and the work run.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_untracked.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that an untracked launch names its device and touches no region. */
TEST(Medium_EmitUntracked, AnUntrackedLaunchNamesItsDevice)
{
    auto runtime = Runtime::create().value();
    int* const data = Testing::shared_integers(runtime, 4, 0);
    ASSERT_NE(data, nullptr);
    const Detail::NodeDescription description =
        Detail::untracked_launch_node(std::size_t{4}, 0, [data](std::size_t index) { data[index] = 1; });
    EXPECT_EQ(description.kind, Detail::NodeKind::Untracked);
    EXPECT_EQ(description.device, 0u);
    EXPECT_EQ(description.width, 4u);
    EXPECT_TRUE(description.reads.empty());
    EXPECT_TRUE(description.writes.empty());
    Testing::run_description(runtime, description);
    for (std::size_t index = 0; index < 4; ++index)
        EXPECT_EQ(data[index], 1);
    sycl::free(data, Testing::queue_of(runtime));
}

/** @brief Tests the range and the raw forms: the width of the range, none for the raw. */
TEST(Medium_EmitUntracked, RangeAndRawForms)
{
    const Detail::NodeDescription range = Detail::untracked_range_node(
        8, 2, [](std::size_t, std::size_t, sycl::handler&) {});
    EXPECT_EQ(range.width, 8u);
    EXPECT_EQ(range.device, 2u);
    const Detail::NodeDescription raw = Detail::untracked_raw_node(1, [](sycl::handler&) {});
    EXPECT_EQ(raw.kind, Detail::NodeKind::Untracked);
    EXPECT_EQ(raw.device, 1u);
}
