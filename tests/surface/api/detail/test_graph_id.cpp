/**************************************************************************/
/* test_graph_id.cpp                                                      */
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
 * @file test_graph_id.cpp
 * @brief Tests GraphId, the process-wide identity of one owner of plans.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/api/detail/graph_id.hpp>

using SushiRuntime::Detail::GraphId;
using SushiRuntime::Detail::take_graph_id;

/** @brief Tests that identities are non-zero and distinct, and a moved-from one reports 0. */
TEST(Small_GraphId, IdsAreNonZeroDistinctAndMovedFromReportsZero)
{
    GraphId first;
    GraphId second;

    EXPECT_NE(first.value(), 0u);
    EXPECT_NE(second.value(), 0u);
    EXPECT_NE(first.value(), second.value());

    const std::uint64_t kept = first.value();
    GraphId moved(std::move(first));
    EXPECT_EQ(moved.value(), kept) << "an identity survives a move";
    EXPECT_EQ(first.value(), 0u) << "the moved-from value stands for no graph";
}

/** @brief Tests that move assignment hands the identity over and leaves the source with none. */
TEST(Small_GraphId, MoveAssignmentHandsTheIdentityOver)
{
    GraphId source;
    GraphId target;
    const std::uint64_t kept = source.value();

    target = std::move(source);

    EXPECT_EQ(target.value(), kept);
    EXPECT_EQ(source.value(), 0u);
}

/** @brief Tests that the counter never repeats an id, even when several threads take them at once. */
TEST(Small_GraphId, SeveralThreadsNeverTakeTheSameId)
{
    constexpr std::size_t K_THREADS = 4;
    constexpr std::size_t K_PER_THREAD = 500;
    std::vector<std::vector<std::uint64_t>> taken(K_THREADS);
    std::vector<std::thread> threads;
    for (std::size_t thread = 0; thread < K_THREADS; ++thread)
        threads.emplace_back([&taken, thread]()
        {
            for (std::size_t i = 0; i < K_PER_THREAD; ++i)
                taken[thread].push_back(take_graph_id());
        });
    for (std::thread& thread : threads)
        thread.join();

    std::set<std::uint64_t> all;
    for (const auto& ids : taken)
        all.insert(ids.begin(), ids.end());
    EXPECT_EQ(all.size(), K_THREADS * K_PER_THREAD);
    EXPECT_EQ(all.count(0), 0u);
}
