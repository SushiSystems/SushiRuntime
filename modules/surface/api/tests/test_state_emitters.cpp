/**************************************************************************/
/* test_state_emitters.cpp                                                */
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
 * @file test_state_emitters.cpp
 * @brief Tests the nodes of State and coupled rules: const inputs, launch width, view-only forms.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>
#include <SushiRuntime/api/detail/emit_dynamic.hpp>
#include <SushiRuntime/api/detail/emit_state.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names the element type of every handle here; unsigned, so overflow wraps. */
    using Word = std::uint32_t;

    constexpr std::size_t WIDTH = 64;
    constexpr std::size_t PREFIX = 24;
    constexpr Word SEED = 5;
    constexpr Word CANARY = 0xDEADBEEFu;

    /** @brief Allocates a State whose current field holds index plus SEED and next CANARY. */
    API::State<Word> seeded_state(Runtime& rt, std::size_t count = WIDTH)
    {
        auto state = rt.state<Word>(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            state[i] = static_cast<Word>(i) + SEED;
            state.next()[i] = CANARY;
        }
        return state;
    }

    /** @brief Returns a view that names the fields a State holds now. */
    std::shared_ptr<API::detail::StateView<Word>> view_of(const API::State<Word>& state)
    {
        auto view = std::make_shared<API::detail::StateView<Word>>();
        view->cur = state.current();
        view->next = state.next();
        return view;
    }
} // namespace

/** @brief Tests that a coupled rule with const inputs computes from both States it reads. */
TEST(Integration_StateEmitters, ACoupledRuleReadsItsInputsThroughConstPointers)
{
    auto rt = Runtime::create();
    auto out = seeded_state(rt);
    auto left = seeded_state(rt);
    auto right = seeded_state(rt);

    auto graph = rt.graph();
    graph.add(API::Coupled(out, left, right),
              [](std::size_t i, const Word* current, Word* next, const Word* a, const Word* b)
              {
                  next[i] = current[i] + a[i] * b[i];
              });
    graph.run();

    for (std::size_t i = 0; i < WIDTH; ++i)
    {
        const Word seed = static_cast<Word>(i) + SEED;
        ASSERT_EQ(out.current()[i], seed + seed * seed) << "at i=" << i;
        ASSERT_EQ(left.current()[i], seed) << "at i=" << i;
        ASSERT_EQ(right.current()[i], seed) << "at i=" << i;
    }
}

/** @brief Tests that State, coupled and untracked nodes report the width of their launch. */
TEST(Integration_StateEmitters, StateCoupledAndUntrackedNodesReportTheirLaunchWidth)
{
    auto rt = Runtime::create();
    auto whole = seeded_state(rt);
    auto prefix = seeded_state(rt);
    auto input = seeded_state(rt);
    auto loose = rt.buffer<Word>(WIDTH);
    Word* words = loose.data();

    auto graph = rt.graph();
    graph.set_profiling(true);
    graph.add(whole, [](std::size_t i, const Word* current, Word* next)
    {
        next[i] = current[i];
    });
    graph.add(PREFIX, API::Coupled(prefix, input),
              [](std::size_t i, const Word* current, Word* next, const Word* other)
              {
                  next[i] = current[i] + other[i];
              });
    graph.add_untracked(PREFIX + 1, [words](std::size_t i) { words[i] = 1u; });

    const auto report = graph.run();

    // The three nodes share no region, so their events are compared sorted by width.
    std::vector<std::size_t> widths;
    for (const auto& event : report.node_events)
        widths.push_back(event.launched_width);
    std::sort(widths.begin(), widths.end());
    EXPECT_EQ(widths, (std::vector<std::size_t>{PREFIX, PREFIX + 1, WIDTH}));
}

/** @brief Tests the State emitter that takes the view and a launch and no State pointer. */
TEST(Integration_StateEmitters, AStateNodeBuiltFromItsViewRunsTheRule)
{
    auto rt = Runtime::create();
    auto field = seeded_state(rt);
    auto emit = API::detail::state_launch_node(
        view_of(field), PREFIX, [](std::size_t i, const Word* current, Word* next)
        {
            next[i] = current[i] + 1u;
        });

    auto graph = rt.graph();
    graph.add_emitter([emit](Graph::TaskGraph& backing) { emit(backing, std::nullopt); });
    graph.run();

    for (std::size_t i = 0; i < WIDTH; ++i)
        ASSERT_EQ(field.next()[i], i < PREFIX ? static_cast<Word>(i) + SEED + 1u : CANARY)
            << "at i=" << i;
}

/** @brief Tests the coupled emitter that takes views and a launch and no State pointer. */
TEST(Integration_StateEmitters, ACoupledNodeBuiltFromItsViewsRunsTheRule)
{
    auto rt = Runtime::create();
    auto out = seeded_state(rt);
    auto input = seeded_state(rt);
    auto emit = API::detail::coupled_launch_node(
        view_of(out), std::make_tuple(view_of(input)), out.size(),
        [](std::size_t i, const Word* current, Word* next, const Word* other)
        {
            next[i] = current[i] + 2u * other[i];
        });

    auto graph = rt.graph();
    graph.add_emitter([emit](Graph::TaskGraph& backing) { emit(backing, std::nullopt); });
    graph.run();

    for (std::size_t i = 0; i < WIDTH; ++i)
        ASSERT_EQ(out.next()[i], 3u * (static_cast<Word>(i) + SEED)) << "at i=" << i;
}

/** @brief Tests the late-bound State emitter that takes the capacity and no State pointer. */
TEST(Integration_StateEmitters, ADynamicStateNodeBuiltFromItsViewRunsTheLiveWindow)
{
    auto rt = Runtime::create();
    auto field = seeded_state(rt);
    auto bindings = std::make_shared<Graph::LateBindings>();
    bindings->count.store(PREFIX);
    auto emit = API::detail::dynamic_state_node(
        view_of(field), field.size(), bindings, nullptr, std::vector<Memory::ResourceRegion>{},
        [](std::size_t i, const Word* current, Word* next) { next[i] = current[i] + 1u; });

    auto graph = rt.graph();
    graph.add_emitter([emit](Graph::TaskGraph& backing) { emit(backing, std::nullopt); });
    graph.run();

    for (std::size_t i = 0; i < WIDTH; ++i)
        ASSERT_EQ(field.next()[i], i < PREFIX ? static_cast<Word>(i) + SEED + 1u : CANARY)
            << "at i=" << i;
}
