/**************************************************************************/
/* test_emit_late_count_resolver.cpp                                      */
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
 * @file test_emit_late_count_resolver.cpp
 * @brief Tests late_count_resolver_node(): the fill receives each count's live value.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_late_count_resolver.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that a fixed and a host count reach the fill as their live values. */
TEST(Unit_EmitLateCountResolver, TheFillReceivesTheLiveCounts)
{
    auto runtime = Runtime::create().value();
    int* const out = Testing::shared_integers(runtime, 2, -1);
    ASSERT_NE(out, nullptr);
    const std::array<LateCount, 2> counts{LateCount::fixed(6), LateCount::from_host(10, [] { return std::size_t{4}; })};
    const Detail::NodeDescription description = Detail::late_count_resolver_node(
        counts, nullptr, [out](const std::array<std::size_t, 2>& live)
        {
            out[0] = static_cast<int>(live[0]);
            out[1] = static_cast<int>(live[1]);
        }, {}, {BufferRegion(out)}, 1);
    Testing::run_description(runtime, description);
    EXPECT_EQ(out[0], 6);
    EXPECT_EQ(out[1], 4);
    sycl::free(out, Testing::queue_of(runtime));
}
