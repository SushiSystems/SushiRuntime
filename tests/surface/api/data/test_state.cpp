/**************************************************************************/
/* test_state.cpp                                                         */
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
 * @file test_state.cpp
 * @brief Tests that the rules on a State follow it when it moves.
 * @see tests/surface/api/data/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/buffer_contents.hpp"
#include "support/host_runtime.hpp"

using namespace SushiRuntime;

static_assert(std::is_nothrow_move_constructible<State<std::uint32_t>>::value,
              "a State must move without throwing");
static_assert(std::is_nothrow_move_assignable<State<std::uint32_t>>::value,
              "a State must move-assign without throwing");
static_assert(!std::is_copy_constructible<State<std::uint32_t>>::value,
              "a State must not copy");

namespace
{
    /** @brief Names the element type of every handle here; unsigned, so overflow wraps. */
    using Word = std::uint32_t;

    constexpr std::size_t K_WIDTH = 64;
    constexpr std::size_t K_PREFIX = 24;
    constexpr std::size_t K_STEPS = 3;
    constexpr Word K_SEED = 5;
    constexpr Word K_CANARY = 0xDEADBEEFu;
    constexpr Word K_MARK = 1000;

    /** @brief Allocates a State whose current field holds index plus K_SEED and next K_CANARY. */
    State<Word> seeded_state(const Runtime& rt)
    {
        auto state = rt.state<Word>(K_WIDTH).value();
        for (std::size_t i = 0; i < K_WIDTH; ++i)
        {
            state[i] = static_cast<Word>(i) + K_SEED;
            state.next()[i] = K_CANARY;
        }
        return state;
    }

    /** @brief Adds the rule next = current + 1 over the whole of field. */
    void add_increment(Graph& graph, State<Word>& field)
    {
        graph.add_rule(field, [](std::size_t i, const Word* current, Word* next)
        {
            next[i] = current[i] + 1u;
        });
    }

    /** @brief Returns the first count words of the current field of @p field. */
    std::vector<Word> current_words(const State<Word>& field, std::size_t count = K_WIDTH)
    {
        return std::vector<Word>(field.current(), field.current() + count);
    }

    /** @brief Returns the words index plus K_SEED plus @p added, for the first @p count indices. */
    std::vector<Word> seeded_words(Word added, std::size_t count = K_WIDTH)
    {
        return TestSupport::ramp<Word>(count, K_SEED + added);
    }

    /** @brief Returns three times index plus K_SEED, for every index. */
    std::vector<Word> tripled_seed_words()
    {
        std::vector<Word> words = seeded_words(0u);
        for (Word& word : words)
            word *= 3u;
        return words;
    }

    /** @brief Appends seeded States to @p fields until the vector reallocates and moves its elements. */
    void push_until_reallocated(const Runtime& rt, std::vector<State<Word>>& fields)
    {
        const auto recorded_at = reinterpret_cast<std::uintptr_t>(&fields[0]);
        while (reinterpret_cast<std::uintptr_t>(&fields[0]) == recorded_at)
            fields.push_back(seeded_state(rt));
    }

    /** @brief Fixture of the tests that record rules on States of a host runtime and move them. */
    class Small_StateMove : public TestSupport::HostRuntimeTest
    {
    };
} // namespace

/** @brief Tests that a rule recorded before a move construction steps the moved-to State. */
TEST_F(Small_StateMove, ARuleFollowsAStateMovedAfterAdd)
{
    const Runtime& rt = runtime();
    auto field = seeded_state(rt);
    auto graph = rt.graph();
    add_increment(graph, field);

    State<Word> moved = std::move(field);
    graph.run(K_STEPS).value();

    EXPECT_EQ(current_words(moved), seeded_words(static_cast<Word>(K_STEPS)));
}

/** @brief Tests that a rule recorded on a vector element survives the vector reallocating. */
TEST_F(Small_StateMove, ARuleFollowsAStateItsVectorReallocated)
{
    const Runtime& rt = runtime();
    std::vector<State<Word>> fields;
    fields.push_back(seeded_state(rt));
    auto graph = rt.graph();
    add_increment(graph, fields[0]);

    push_until_reallocated(rt, fields);
    graph.run(K_STEPS).value();

    EXPECT_EQ(current_words(fields[0]), seeded_words(static_cast<Word>(K_STEPS)));
    EXPECT_EQ(current_words(fields[1]), seeded_words(0u));
}

/** @brief Tests that a coupled rule follows the State it writes and the State it reads. */
TEST_F(Small_StateMove, ACoupledRuleFollowsItsMovedStates)
{
    const Runtime& rt = runtime();
    auto out = seeded_state(rt);
    auto input = seeded_state(rt);
    auto graph = rt.graph();
    graph.add_coupled(Coupled(out, input),
                      [](std::size_t i, const Word* current, Word* next, const Word* other)
                      {
                          next[i] = current[i] + other[i];
                      });

    State<Word> moved_input = std::move(input);
    State<Word> moved_out = std::move(out);
    graph.run(std::size_t{2}).value();

    // Two steps of out += input, with input never written, leave three times the seed.
    EXPECT_EQ(current_words(moved_out), tripled_seed_words());
    EXPECT_EQ(current_words(moved_input), seeded_words(0u));
}

/** @brief Tests that a late-bound State rule follows the State through a move. */
TEST_F(Small_StateMove, ADynamicStateRuleFollowsAMovedState)
{
    const Runtime& rt = runtime();
    auto field = seeded_state(rt);
    std::size_t live = K_PREFIX;
    auto graph = rt.graph();
    graph.add_dynamic(sized([&live] { return live; }), field,
                      [](std::size_t i, const Word* current, Word* next) { next[i] = current[i] + 1u; });

    State<Word> moved = std::move(field);
    graph.run(K_STEPS).value();

    EXPECT_EQ(current_words(moved, K_PREFIX), seeded_words(static_cast<Word>(K_STEPS), K_PREFIX));
}

/** @brief Tests that a State assigned over after add() is refused at the next run. */
TEST_F(Small_StateMove, AStateAssignedOverRefusesTheRun)
{
    const Runtime& rt = runtime();
    auto field = seeded_state(rt);
    auto graph = rt.graph();
    add_increment(graph, field);
    ASSERT_TRUE(graph.run());

    field = seeded_state(rt);

    const auto refused = graph.run();
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::DanglingResource);
}

/** @brief Tests that a late-bound State rule replayed after one step reads the current field. */
TEST_F(Small_StateMove, ADynamicStateRuleReplayedAfterAStepReadsTheCurrentField)
{
    const Runtime& rt = runtime();
    auto field = seeded_state(rt);
    Word* const second = field.next();
    std::size_t live = K_WIDTH;

    auto graph = rt.graph();
    graph.add(K_WIDTH, Reads{}, Writes(field),
              [second](std::size_t i) { second[i] = K_MARK; });
    graph.add_dynamic(sized([&live] { return live; }), field,
                      [](std::size_t i, const Word* current, Word* next) { next[i] = current[i] + 1u; });
    graph.run().value();
    ASSERT_EQ(field.current(), second);

    // The first rule still writes `second`, which is now the field the State rule reads.
    graph.name("rebuilt");
    graph.run().value();

    EXPECT_EQ(current_words(field), std::vector<Word>(K_WIDTH, K_MARK + 1u));
}
