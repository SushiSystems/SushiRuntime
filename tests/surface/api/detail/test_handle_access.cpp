/**************************************************************************/
/* test_handle_access.cpp                                                 */
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
 * @file test_handle_access.cpp
 * @brief Tests HandleAccess: the keys and the liveness of a Buffer and a State.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that a buffer's key is its data and its liveness ends with it. */
TEST(Medium_HandleAccess, ABufferKeyIsItsData)
{
    auto runtime = Runtime::create().value();
    LivenessObserver life;
    {
        auto buffer = runtime.buffer<int>(4).value();
        EXPECT_EQ(Detail::HandleAccess::key(buffer), static_cast<void*>(buffer.data()));
        life = Detail::HandleAccess::liveness(buffer);
        EXPECT_FALSE(life.expired());
    }
    EXPECT_TRUE(life.expired()) << "a destroyed buffer still reported itself alive";
}

/** @brief Tests that a state reads its current field and writes its next one. */
TEST(Medium_HandleAccess, AStateReadsCurrentAndWritesNext)
{
    auto runtime = Runtime::create().value();
    auto state = runtime.state<int>(4).value();
    EXPECT_EQ(Detail::HandleAccess::read_key(state), static_cast<void*>(state.current()));
    EXPECT_EQ(Detail::HandleAccess::write_key(state), static_cast<void*>(state.next()));
}
