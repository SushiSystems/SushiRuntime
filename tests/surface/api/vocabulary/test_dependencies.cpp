/**************************************************************************/
/* test_dependencies.cpp                                                  */
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
 * @file test_dependencies.cpp
 * @brief Tests the dependency keys: a buffer's data, a state's two fields, and a region as given.
 * @see tests/surface/api/vocabulary/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/vocabulary/dependencies.hpp>

using namespace SushiRuntime;

/** @brief Tests that a buffer reads and writes one key, and a state reads current and writes next. */
TEST(Unit_Dependencies, KeysFollowTheHandle)
{
    auto runtime = Runtime::create().value();
    auto buffer = runtime.buffer<int>(4).value();
    auto state = runtime.state<int>(4).value();
    EXPECT_EQ(Detail::read_key_of(buffer).base, Detail::write_key_of(buffer).base);
    EXPECT_EQ(Detail::read_key_of(state).base, static_cast<void*>(state.current()));
    EXPECT_EQ(Detail::write_key_of(state).base, static_cast<void*>(state.next()));
    int raw = 0;
    const BufferRegion region(&raw, 0, sizeof(int));
    EXPECT_EQ(Detail::read_key_of(region).base, &raw);
    EXPECT_FALSE(Detail::liveness_of(buffer).expired());
}

/** @brief Tests that Reads and Writes collect one key per handle, in order. */
TEST(Unit_Dependencies, ReadsAndWritesCollectOneKeyEach)
{
    auto runtime = Runtime::create().value();
    auto first = runtime.buffer<int>(4).value();
    auto second = runtime.buffer<int>(4).value();
    const Reads reads(first, second);
    ASSERT_EQ(reads.keys.size(), 2u);
    EXPECT_EQ(reads.keys[0].base, static_cast<void*>(first.data()));
    EXPECT_EQ(reads.keys[1].base, static_cast<void*>(second.data()));
    const Writes writes(first);
    EXPECT_EQ(writes.keys.size(), 1u);
}
