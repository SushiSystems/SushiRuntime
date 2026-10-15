/**************************************************************************/
/* test_common.cpp                                                        */
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
 * @file test_common.cpp
 * @brief Tests the constants of common.hpp: their values and the powers of two they promise.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/core/common.hpp>

using namespace SushiRuntime;

/** @brief Tests the values the scheduler and the allocators are sized by. */
TEST(Small_Common, ConstantsHoldTheirDocumentedValues)
{
    static_assert(K_CACHE_LINE_SIZE == 64, "a cache line is 64 bytes");
    static_assert(K_MAX_NUMA_NODES == 64, "a node mask holds 64 nodes");
    static_assert(K_MAX_WORKER_THREADS == 256, "the pool holds at most 256 workers");
    static_assert(K_DEFAULT_ALIGNMENT == 128, "the default alignment is two cache lines");
    SUCCEED();
}

/** @brief Tests that both alignments are powers of two and the default covers a cache line. */
TEST(Small_Common, AlignmentsArePowersOfTwo)
{
    const auto is_power_of_two = [](std::size_t value) { return value != 0 && (value & (value - 1)) == 0; };
    EXPECT_TRUE(is_power_of_two(K_CACHE_LINE_SIZE));
    EXPECT_TRUE(is_power_of_two(K_DEFAULT_ALIGNMENT));
    EXPECT_EQ(K_DEFAULT_ALIGNMENT % K_CACHE_LINE_SIZE, 0u);
}
