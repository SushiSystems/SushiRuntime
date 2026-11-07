/**************************************************************************/
/* test_emit_state.cpp                                                    */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_emit_state.cpp
 * @brief Tests the descriptions the state emitters build from their views, and their work.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_dynamic.hpp>
#include <SushiRuntime/api/detail/emit_state.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>

#include "description_runner.hpp"
#include "run_recorder.hpp"
#include "support/buffer_contents.hpp"
#include "support/host_runtime.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Names the element type of every handle here; unsigned, so overflow wraps. */
    using Word = std::uint32_t;

    constexpr std::size_t K_WIDTH = 64;
    constexpr std::size_t K_PREFIX = 24;
    constexpr Word K_SEED = 5;
    constexpr Word K_CANARY = 0xDEADBEEFu;

    /** @brief Allocates a State whose current field holds index plus K_SEED and next K_CANARY. */
    State<Word> seeded_state(const Runtime& rt, std::size_t count = K_WIDTH)
    {
        auto state = rt.state<Word>(count).value();
        for (std::size_t i = 0; i < count; ++i)
        {
            state[i] = static_cast<Word>(i) + K_SEED;
            state.next()[i] = K_CANARY;
        }
        return state;
    }

    /** @brief Returns the view a State owns and shares with the graphs that record it. */
    std::shared_ptr<Detail::StateView<Word>> view_of(const State<Word>& state)
    {
        return Detail::HandleAccess::view(state);
    }

    /** @brief Returns the first count words of the current field of @p state. */
    std::vector<Word> current_words(const State<Word>& state, std::size_t count = K_WIDTH)
    {
        return std::vector<Word>(state.current(), state.current() + count);
    }

    /** @brief Returns the whole next field of @p state. */
    std::vector<Word> next_words(const State<Word>& state)
    {
        return std::vector<Word>(state.next(), state.next() + state.size());
    }

    /** @brief Returns the words index plus K_SEED, each multiplied by @p factor. */
    std::vector<Word> seed_words_times(Word factor)
    {
        std::vector<Word> words = TestSupport::ramp<Word>(K_WIDTH, K_SEED);
        for (Word& word : words)
            word *= factor;
        return words;
    }

    /** @brief Returns seed + seed * seed at every index: out + left * right, all three seeded alike. */
    std::vector<Word> seed_plus_square_words()
    {
        std::vector<Word> words = TestSupport::ramp<Word>(K_WIDTH, K_SEED);
        for (Word& word : words)
            word += word * word;
        return words;
    }

    /** @brief Returns the next field after a rule that adds one to the first @p live words. */
    std::vector<Word> next_after_increment_of(std::size_t live)
    {
        std::vector<Word> words(K_WIDTH, K_CANARY);
        for (std::size_t i = 0; i < live; ++i)
            words[i] = static_cast<Word>(i) + K_SEED + 1u;
        return words;
    }

    /** @brief Returns the launched widths of the events of @p observation, sorted. */
    std::vector<std::size_t> sorted_launched_widths(const RunObservation& observation)
    {
        std::vector<std::size_t> widths;
        for (const auto& event : observation.events)
            widths.push_back(event.launched_width);
        std::sort(widths.begin(), widths.end());
        return widths;
    }

    /** @brief Fixture of the tests that build state nodes on a host runtime. */
    class Small_StateEmitters : public TestSupport::HostRuntimeTest
    {
    };
} // namespace

/** @brief Tests that a coupled rule with const inputs computes from both States it reads. */
TEST_F(Small_StateEmitters, ACoupledRuleReadsItsInputsThroughConstPointers)
{
    const Runtime& rt = runtime();
    auto out = seeded_state(rt);
    auto left = seeded_state(rt);
    auto right = seeded_state(rt);
    auto graph = rt.graph();
    graph.add_coupled(Coupled(out, left, right),
                      [](std::size_t i, const Word* current, Word* next, const Word* a, const Word* b)
                      {
                          next[i] = current[i] + a[i] * b[i];
                      });

    graph.run().value();

    EXPECT_EQ(current_words(out), seed_plus_square_words());
    EXPECT_EQ(current_words(left), seed_words_times(1u));
    EXPECT_EQ(current_words(right), seed_words_times(1u));
}

/** @brief Tests that State, coupled and untracked nodes report the width of their launch. */
TEST_F(Small_StateEmitters, StateCoupledAndUntrackedNodesReportTheirLaunchWidth)
{
    const Runtime& rt = runtime();
    auto whole = seeded_state(rt);
    auto prefix = seeded_state(rt);
    auto input = seeded_state(rt);
    auto loose = rt.buffer<Word>(K_WIDTH).value();
    Word* words = loose.data();

    Testing::RunRecorder recorder(rt);
    auto graph = rt.graph();
    graph.add_rule(whole, [](std::size_t i, const Word* current, Word* next)
    {
        next[i] = current[i];
    });
    graph.add_coupled(K_PREFIX, Coupled(prefix, input),
                      [](std::size_t i, const Word* current, Word* next, const Word* other)
                      {
                          next[i] = current[i] + other[i];
                      });
    graph.add_untracked(K_PREFIX + 1, [words](std::size_t i) { words[i] = 1u; });

    graph.run().value();

    ASSERT_EQ(recorder.count(), 1u);
    // The three nodes share no region, so their events are compared sorted by width.
    EXPECT_EQ(sorted_launched_widths(recorder.last()),
              (std::vector<std::size_t>{K_PREFIX, K_PREFIX + 1, K_WIDTH}));
}

/** @brief Tests the State emitter over a launch, added outside a recorder. */
TEST_F(Small_StateEmitters, AStateNodeBuiltFromItsViewRunsTheRule)
{
    auto field = seeded_state(runtime());
    const Detail::NodeDescription described = Detail::state_launch_node(
        view_of(field), K_PREFIX, [](std::size_t i, const Word* current, Word* next)
        {
            next[i] = current[i] + 1u;
        });

    described.host_form();

    EXPECT_EQ(next_words(field), next_after_increment_of(K_PREFIX));
}

/** @brief Tests the coupled emitter over a launch, added outside a recorder. */
TEST_F(Small_StateEmitters, ACoupledNodeBuiltFromItsViewsRunsTheRule)
{
    auto out = seeded_state(runtime());
    auto input = seeded_state(runtime());
    const Detail::NodeDescription described = Detail::coupled_launch_node(
        view_of(out), std::make_tuple(view_of(input)), out.size(),
        [](std::size_t i, const Word* current, Word* next, const Word* other)
        {
            next[i] = current[i] + 2u * other[i];
        });

    described.host_form();

    EXPECT_EQ(next_words(out), seed_words_times(3u));
}

/** @brief Tests the late-bound State emitter, added outside a recorder. */
TEST_F(Small_StateEmitters, ADynamicStateNodeBuiltFromItsViewRunsTheLiveWindow)
{
    auto field = seeded_state(runtime());
    const Detail::NodeDescription described = Detail::dynamic_state_node(
        view_of(field), field.size(), Testing::bound_controls(sized([] { return K_PREFIX; })),
        nullptr, std::vector<BufferRegion>{},
        [](std::size_t i, const Word* current, Word* next) { next[i] = current[i] + 1u; });

    described.host_form();

    EXPECT_EQ(next_words(field), next_after_increment_of(K_PREFIX));
}
