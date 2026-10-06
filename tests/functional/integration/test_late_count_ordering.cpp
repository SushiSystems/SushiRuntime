/**************************************************************************/
/* test_late_count_ordering.cpp                                           */
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
 * @file test_late_count_ordering.cpp
 * @brief Tests that a device LateCount resolves when its consumer dispatches, after its producer.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

/**
 * @brief Tests that a late-bound add_reduce sees the count its predecessor wrote in the same run.
 */
TEST(Integration_LateCountOrdering, ReduceSeesTheCountItsPredecessorJustWrote)
{
    constexpr std::size_t CAPACITY = 1000;

    auto rt      = Runtime::create();
    auto values  = rt.buffer<double>(CAPACITY);
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    auto source  = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    auto out     = rt.buffer<double>(1);

    for (std::size_t i = 0; i < CAPACITY; ++i)
        values[i] = 1.0;

    // A device kernel cannot reach this frame, so its next count is in a buffer; see README.md.
    const std::uint32_t* source_ptr = source.data();
    std::uint32_t* counter_ptr = counter.data();

    LateCount lc;
    lc.capacity = CAPACITY;
    lc.device = &counter;

    auto g = rt.graph();
    g.add(Reads(source), Writes(counter), std::size_t{1},
          [counter_ptr, source_ptr](std::size_t) { counter_ptr[0] = source_ptr[0]; });
    g.add_reduce(values, out, lc, Sum<double>{});

    source[0] = 300;
    g.run();
    EXPECT_DOUBLE_EQ(out[0], 300.0)
        << "the reduce must fold the count its own step's producer wrote";

    source[0] = 700;
    g.run();
    EXPECT_DOUBLE_EQ(out[0], 700.0)
        << "a changed count must take effect the same run, not one run late";

    EXPECT_EQ(g.compile_count(), 1u);
}
