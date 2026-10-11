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
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/node.hpp>

using namespace SushiRuntime;

static_assert(std::is_nothrow_move_constructible<API::State<std::uint32_t>>::value,
              "a State must move without throwing");
static_assert(std::is_nothrow_move_assignable<API::State<std::uint32_t>>::value,
              "a State must move-assign without throwing");
static_assert(!std::is_copy_constructible<API::State<std::uint32_t>>::value,
              "a State must not copy");

namespace
{
    /** @brief Names the element type of every handle here; unsigned, so overflow wraps. */
    using Word = std::uint32_t;

    constexpr std::size_t WIDTH = 64;
    constexpr std::size_t PREFIX = 24;
    constexpr std::size_t STEPS = 3;
    constexpr Word SEED = 5;
    constexpr Word CANARY = 0xDEADBEEFu;
    constexpr Word MARK = 1000;

    /** @brief Allocates a State whose current field holds index plus SEED and next CANARY. */
    API::State<Word> seeded_state(Runtime& rt)
    {
        auto state = rt.state<Word>(WIDTH);
        for (std::size_t i = 0; i < WIDTH; ++i)
        {
            state[i] = static_cast<Word>(i) + SEED;
            state.next()[i] = CANARY;
        }
        return state;
    }

    /** @brief Adds the rule next = current + 1 over the whole of field. */
    void add_increment(API::Graph& graph, API::State<Word>& field)
    {
        graph.add(field, [](std::size_t i, const Word* current, Word* next)
        {
            next[i] = current[i] + 1u;
        });
    }

    /** @brief Expects the first count words of field to hold index plus SEED plus added. */
    void expect_advanced(const API::State<Word>& field, Word added, std::size_t count = WIDTH)
    {
        ASSERT_EQ(field.size(), WIDTH);
        for (std::size_t i = 0; i < count; ++i)
            ASSERT_EQ(field.current()[i], static_cast<Word>(i) + SEED + added) << "at i=" << i;
    }
} // namespace

/** @brief Tests that a rule recorded before a move construction steps the moved-to State. */
TEST(Integration_StateMove, ARuleFollowsAStateMovedAfterAdd)
{
    auto rt = Runtime::create();
    auto field = seeded_state(rt);
    auto graph = rt.graph();
    add_increment(graph, field);

    API::State<Word> moved = std::move(field);
    graph.run(STEPS);

    expect_advanced(moved, static_cast<Word>(STEPS));
}

/** @brief Tests that a rule recorded on a vector element survives the vector reallocating. */
TEST(Integration_StateMove, ARuleFollowsAStateItsVectorReallocated)
{
    auto rt = Runtime::create();
    std::vector<API::State<Word>> fields;
    fields.push_back(seeded_state(rt));
    auto graph = rt.graph();
    add_increment(graph, fields[0]);

    const auto recorded_at = reinterpret_cast<std::uintptr_t>(&fields[0]);
    while (reinterpret_cast<std::uintptr_t>(&fields[0]) == recorded_at)
        fields.push_back(seeded_state(rt));
    graph.run(STEPS);

    expect_advanced(fields[0], static_cast<Word>(STEPS));
    expect_advanced(fields[1], 0u);
}

/** @brief Tests that a coupled rule follows the State it writes and the State it reads. */
TEST(Integration_StateMove, ACoupledRuleFollowsItsMovedStates)
{
    auto rt = Runtime::create();
    auto out = seeded_state(rt);
    auto input = seeded_state(rt);
    auto graph = rt.graph();
    graph.add(API::Coupled(out, input),
              [](std::size_t i, const Word* current, Word* next, const Word* other)
              {
                  next[i] = current[i] + other[i];
              });

    API::State<Word> moved_input = std::move(input);
    API::State<Word> moved_out = std::move(out);
    graph.run(std::size_t{2});

    for (std::size_t i = 0; i < WIDTH; ++i)
    {
        const Word seed = static_cast<Word>(i) + SEED;
        ASSERT_EQ(moved_out.current()[i], 3u * seed) << "at i=" << i;
        ASSERT_EQ(moved_input.current()[i], seed) << "at i=" << i;
    }
}

/** @brief Tests that a late-bound State rule follows the State through a move. */
TEST(Integration_StateMove, ADynamicStateRuleFollowsAMovedState)
{
    auto rt = Runtime::create();
    auto field = seeded_state(rt);
    std::size_t live = PREFIX;
    auto graph = rt.graph();
    graph.add(API::sized([&live] { return live; }), field,
              [](std::size_t i, const Word* current, Word* next) { next[i] = current[i] + 1u; });

    API::State<Word> moved = std::move(field);
    graph.run(STEPS);

    EXPECT_EQ(graph.compile_count(), 1u);
    expect_advanced(moved, static_cast<Word>(STEPS), PREFIX);
}

/** @brief Tests what a State writes into its view at an advance, a move and its destruction. */
TEST(Integration_StateMove, TheViewFollowsAdvanceMoveAndDestruction)
{
    using API::detail::HandleAccess;

    auto rt = Runtime::create();
    std::shared_ptr<API::detail::StateView<Word>> view;
    {
        auto field = seeded_state(rt);
        Word* const first = field.current();
        Word* const second = field.next();
        view = HandleAccess::view(field);
        EXPECT_EQ(view->current, first);
        EXPECT_EQ(view->next, second);
        EXPECT_EQ(view->size, WIDTH);

        HandleAccess::advance(field);
        EXPECT_EQ(view->current, second);
        EXPECT_EQ(view->next, first);

        API::State<Word> moved = std::move(field);
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

    API::State<Word> empty;
    const auto unowned = HandleAccess::view(empty);
    ASSERT_NE(unowned, nullptr);
    EXPECT_EQ(unowned->current, nullptr);
    EXPECT_EQ(unowned->size, 0u);
}

/** @brief Tests that a State assigned over after add() is refused at the next run. */
TEST(Integration_StateMove, AStateAssignedOverRefusesTheRun)
{
    auto rt = Runtime::create();
    auto field = seeded_state(rt);
    auto graph = rt.graph();
    add_increment(graph, field);
    ASSERT_TRUE(graph.try_run());

    field = seeded_state(rt);

    const auto refused = graph.try_run();
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, Core::Errc::dangling_resource);
}

/** @brief Tests that a late-bound State rule replayed after one step reads the current field. */
TEST(Integration_StateMove, ADynamicStateRuleReplayedAfterAStepReadsTheCurrentField)
{
    auto rt = Runtime::create();
    auto field = seeded_state(rt);
    Word* const second = field.next();
    std::size_t live = WIDTH;

    auto graph = rt.graph();
    graph.add(WIDTH, API::Reads{}, API::Writes(field),
              [second](std::size_t i) { second[i] = MARK; });
    graph.add(API::sized([&live] { return live; }), field,
              [](std::size_t i, const Word* current, Word* next) { next[i] = current[i] + 1u; });
    graph.run();
    ASSERT_EQ(field.current(), second);

    // The first rule still writes `second`, which is now the field the State rule reads.
    Graph::TaskGraph replayed;
    std::int32_t waited_on = -1;
    graph.recorder().replay(replayed, [&](std::size_t rule, std::size_t)
    {
        if (rule == 1)
            waited_on = replayed.last_node_added()->in_degree.load();
    });
    EXPECT_EQ(waited_on, 1) << "the State rule must wait on the writer of its current field";

    graph.name("rebuilt");
    graph.run();

    EXPECT_EQ(graph.compile_count(), 2u);
    for (std::size_t i = 0; i < WIDTH; ++i)
        ASSERT_EQ(field.current()[i], MARK + 1u) << "at i=" << i;
}
