/**************************************************************************/
/* test_device_slot_liveness.cpp                                          */
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
 * @file test_device_slot_liveness.cpp
 * @brief Tests that a graph watches the buffer a Dynamic reads its count or base from.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names the element type of every handle here. */
    using Word = std::uint32_t;

    constexpr std::size_t K_WIDTH = 64;
    constexpr Word K_LIVE = 24;

    /** @brief Allocates a one-word Residency::Host buffer that holds value. */
    Buffer<Word> host_slot(Runtime& rt, Word value)
    {
        auto slot = rt.buffer<Word>(1, DeviceIndex{0}, Residency::Host);
        slot[0] = value;
        return slot;
    }
} // namespace

/** @brief Tests that a run is refused once the buffer of sized_from_device() is destroyed. */
TEST(Integration_DeviceSlotLiveness, ADestroyedCountBufferRefusesTheRun)
{
    auto rt = Runtime::create();
    auto target = rt.buffer<Word>(K_WIDTH);
    std::optional<Buffer<Word>> counter(host_slot(rt, K_LIVE));
    Word* words = target.data();

    auto graph = rt.graph();
    graph.add(sized_from_device(*counter, 0), K_WIDTH, Reads(), Writes(target),
              [words](std::size_t i) { words[i] = 1u; });
    EXPECT_EQ(graph.recorder().watched_allocation_count(), 2u)
        << "the target and the counter are both watched";
    ASSERT_TRUE(graph.try_run());

    counter.reset();

    EXPECT_TRUE(graph.recorder().has_dangling_resource());
    auto refused = graph.try_run();
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, Errc::dangling_resource);
}

/** @brief Tests that a run is refused once the buffer of based_at_device() is destroyed. */
TEST(Integration_DeviceSlotLiveness, ADestroyedBaseBufferRefusesTheRun)
{
    auto rt = Runtime::create();
    auto target = rt.buffer<Word>(K_WIDTH);
    std::optional<Buffer<Word>> base(host_slot(rt, 0u));
    Word* words = target.data();

    auto graph = rt.graph();
    graph.add(based_at_device(*base), K_WIDTH, Reads(), Writes(target),
              [words](std::size_t i) { words[i] = 1u; });
    ASSERT_TRUE(graph.try_run());

    base.reset();

    auto refused = graph.try_run();
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, Errc::dangling_resource);
}

/** @brief Tests that a late-bound State rule watches its count buffer as well as its State. */
TEST(Integration_DeviceSlotLiveness, ADestroyedCountBufferRefusesAStateRule)
{
    auto rt = Runtime::create();
    auto field = rt.state<Word>(K_WIDTH);
    std::optional<Buffer<Word>> counter(host_slot(rt, K_LIVE));

    auto graph = rt.graph();
    graph.add(sized_from_device(*counter), field,
              [](std::size_t i, const Word* current, Word* next) { next[i] = current[i]; });
    ASSERT_TRUE(graph.try_run());

    counter.reset();

    auto refused = graph.try_run();
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, Errc::dangling_resource);
}
