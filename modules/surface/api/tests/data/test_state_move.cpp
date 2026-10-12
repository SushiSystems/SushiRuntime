/**************************************************************************/
/* test_state_move.cpp                                                    */
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
 * @file test_state_move.cpp
 * @brief Tests that a recorded State rule follows the State through a move and a rebuilt plan.
 * @see modules/surface/api/tests/data/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>

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
    State<Word> seeded_state(Runtime& rt)
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

    /** @brief Expects the first count words of field to hold index plus K_SEED plus added. */
    void expect_advanced(const State<Word>& field, Word added, std::size_t count = K_WIDTH)
    {
        ASSERT_EQ(field.size(), K_WIDTH);
        for (std::size_t i = 0; i < count; ++i)
            ASSERT_EQ(field.current()[i], static_cast<Word>(i) + K_SEED + added) << "at i=" << i;
    }
} // namespace

/** @brief Tests that a rule recorded before a move construction steps the moved-to State. */
TEST(Integration_StateMove, ARuleFollowsAStateMovedAfterAdd)
{
    auto rt = Runtime::create().value();
    auto field = seeded_state(rt);
    auto graph = rt.graph();
    add_increment(graph, field);

    State<Word> moved = std::move(field);
    graph.run(K_STEPS).value();

    expect_advanced(moved, static_cast<Word>(K_STEPS));
}

/** @brief Tests that a rule recorded on a vector element survives the vector reallocating. */
TEST(Integration_StateMove, ARuleFollowsAStateItsVectorReallocated)
{
    auto rt = Runtime::create().value();
    std::vector<State<Word>> fields;
    fields.push_back(seeded_state(rt));
    auto graph = rt.graph();
    add_increment(graph, fields[0]);

    const auto recorded_at = reinterpret_cast<std::uintptr_t>(&fields[0]);
    while (reinterpret_cast<std::uintptr_t>(&fields[0]) == recorded_at)
        fields.push_back(seeded_state(rt));
    graph.run(K_STEPS).value();

    expect_advanced(fields[0], static_cast<Word>(K_STEPS));
    expect_advanced(fields[1], 0u);
}

/** @brief Tests that a coupled rule follows the State it writes and the State it reads. */
TEST(Integration_StateMove, ACoupledRuleFollowsItsMovedStates)
{
    auto rt = Runtime::create().value();
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

    for (std::size_t i = 0; i < K_WIDTH; ++i)
    {
        const Word seed = static_cast<Word>(i) + K_SEED;
        ASSERT_EQ(moved_out.current()[i], 3u * seed) << "at i=" << i;
        ASSERT_EQ(moved_input.current()[i], seed) << "at i=" << i;
    }
}

/** @brief Tests that a late-bound State rule follows the State through a move. */
TEST(Integration_StateMove, ADynamicStateRuleFollowsAMovedState)
{
    auto rt = Runtime::create().value();
    auto field = seeded_state(rt);
    std::size_t live = K_PREFIX;
    auto graph = rt.graph();
    graph.add_dynamic(sized([&live] { return live; }), field,
                      [](std::size_t i, const Word* current, Word* next) { next[i] = current[i] + 1u; });

    State<Word> moved = std::move(field);
    graph.run(K_STEPS).value();

    EXPECT_EQ(graph.compile_count(), 1u);
    expect_advanced(moved, static_cast<Word>(K_STEPS), K_PREFIX);
}

/** @brief Tests what a State writes into its view at an advance, a move and its destruction. */
TEST(Integration_StateMove, TheViewFollowsAdvanceMoveAndDestruction)
{
    using Detail::HandleAccess;

    auto rt = Runtime::create().value();
    std::shared_ptr<Detail::StateView<Word>> view;
    {
        auto field = seeded_state(rt);
        Word* const first = field.current();
        Word* const second = field.next();
        view = HandleAccess::view(field);
        EXPECT_EQ(view->current, first);
        EXPECT_EQ(view->next, second);
        EXPECT_EQ(view->size, K_WIDTH);

        HandleAccess::advance(field);
        EXPECT_EQ(view->current, second);
        EXPECT_EQ(view->next, first);

        State<Word> moved = std::move(field);
        EXPECT_EQ(HandleAccess::view(moved), view) << "the moved-to State must keep the view";
        EXPECT_EQ(view->current, second);

        HandleAccess::advance(*view);
        EXPECT_EQ(moved.current(), first) << "an advance through the view must swap the State";
        EXPECT_EQ(view->current, first);
    }
    EXPECT_EQ(view->current, nullptr);
    EXPECT_EQ(view->next, nullptr);
    EXPECT_EQ(view->size, 0u);
    HandleAccess::advance(*view);
    EXPECT_EQ(view->current, nullptr) << "a view without its State must stay empty";

    State<Word> empty;
    const auto unowned = HandleAccess::view(empty);
    ASSERT_NE(unowned, nullptr);
    EXPECT_EQ(unowned->current, nullptr);
    EXPECT_EQ(unowned->size, 0u);
}

/** @brief Tests that a State assigned over after add() is refused at the next run. */
TEST(Integration_StateMove, AStateAssignedOverRefusesTheRun)
{
    auto rt = Runtime::create().value();
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
TEST(Integration_StateMove, ADynamicStateRuleReplayedAfterAStepReadsTheCurrentField)
{
    auto rt = Runtime::create().value();
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
    const auto plan = graph.plan();
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->nodes.size(), 2u);
    const PlanNode& state_rule = plan->nodes[1];
    EXPECT_EQ(state_rule.rule, 1u);
    EXPECT_EQ(state_rule.predecessor_count, 1u)
        << "the State rule must wait on the writer of its current field";
    ASSERT_EQ(state_rule.reads.size(), 1u);
    EXPECT_EQ(state_rule.reads[0].base, static_cast<void*>(second));

    graph.run().value();

    EXPECT_EQ(graph.compile_count(), 2u);
    for (std::size_t i = 0; i < K_WIDTH; ++i)
        ASSERT_EQ(field.current()[i], K_MARK + 1u) << "at i=" << i;
}
