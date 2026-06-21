/**************************************************************************/
/* test_dependency_tracker.cpp                                          */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// Unit coverage for the lock-free DependencyTracker hazard resolution. Pins the
// edge semantics on raw nodes: RAW/WAR/WAW edges, two readers stay parallel
// (RAR), shared buffers dedup to one edge, and no self-dependency. in_degree is
// the observable — the count of incoming edges the scheduler decrements.

#include <gtest/gtest.h>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/graph/dependency_tracker.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Graph;

namespace
{
    /**
     * @brief Helper to retrieve the in_degree of a node.
     *
     * @param n The node to inspect.
     * @return The current in_degree count.
     */
    int in_degree_of(const sushi_ptr<Node>& n)
    {
        return n->in_degree.load(std::memory_order_relaxed);
    }
}

/**
 * @brief Tests that a read after a write creates a single RAW edge.
 *
 * Simulates a writer followed by a reader accessing the same buffer, ensuring
 * the reader's incoming edge count increments.
 */
TEST(Unit_DependencyTrackerTest, RawCreatesEdge)
{
    DependencyTracker tracker;
    auto writer = make_sushi<Node>();
    auto reader = make_sushi<Node>();

    int buffer = 0;
    void* p = &buffer;

    tracker.resolve(writer.get(), p, true);
    tracker.resolve(reader.get(), p, false);

    EXPECT_EQ(in_degree_of(writer), 0);
    EXPECT_EQ(in_degree_of(reader), 1) << "Reader must depend on the preceding writer (RAW).";

    tracker.reset();
}

/**
 * @brief Tests WAR and WAW edge tracking in a chain of dependencies.
 *
 * Creates a sequence of Write -> Read -> Write and verifies the correct
 * dependency counts for all operations.
 */
TEST(Unit_DependencyTrackerTest, WarAndWawChain)
{
    DependencyTracker tracker;
    auto w1 = make_sushi<Node>();
    auto rd = make_sushi<Node>();
    auto w2 = make_sushi<Node>();

    int buffer = 0;
    void* p = &buffer;

    tracker.resolve(w1.get(), p, true);   // initial write
    tracker.resolve(rd.get(), p, false);  // RAW on w1
    tracker.resolve(w2.get(), p, true);   // WAR on rd + WAW on w1

    EXPECT_EQ(in_degree_of(w1), 0);
    EXPECT_EQ(in_degree_of(rd), 1) << "Read must depend on the prior write (RAW).";
    EXPECT_EQ(in_degree_of(w2), 2) << "Write must depend on the intervening read AND the prior write.";

    tracker.reset();
}

/**
 * @brief Tests that multiple readers of the same writer don't depend on each other.
 *
 * Ensures that Read-After-Read (RAR) operations remain parallel and only
 * depend on the preceding write.
 */
TEST(Unit_DependencyTrackerTest, ReadersDependOnWriterNotEachOther)
{
    DependencyTracker tracker;
    auto writer = make_sushi<Node>();
    auto r1 = make_sushi<Node>();
    auto r2 = make_sushi<Node>();

    int buffer = 0;
    void* p = &buffer;

    tracker.resolve(writer.get(), p, true);
    tracker.resolve(r1.get(), p, false);
    tracker.resolve(r2.get(), p, false);

    EXPECT_EQ(in_degree_of(r1), 1);
    EXPECT_EQ(in_degree_of(r2), 1) << "Second reader must depend only on the writer, not on the first reader (RAR is parallel).";

    tracker.reset();
}

/**
 * @brief Tests that multiple shared buffers between the same nodes create only one edge.
 *
 * When a writer and a reader access two distinct buffers, the dependency tracker
 * should deduplicate this into a single logical dependency edge.
 */
TEST(Unit_DependencyTrackerTest, SharedBuffersCreateSingleEdge)
{
    DependencyTracker tracker;
    auto writer = make_sushi<Node>();
    auto reader = make_sushi<Node>();

    int a = 0, b = 0;
    void* pa = &a;
    void* pb = &b;

    tracker.resolve(writer.get(), pa, true);
    tracker.resolve(writer.get(), pb, true);
    tracker.resolve(reader.get(), pa, false); // edge writer -> reader
    tracker.resolve(reader.get(), pb, false); // same edge again -> must dedup

    EXPECT_EQ(in_degree_of(reader), 1) << "Two shared buffers between the same pair must add only one edge.";

    tracker.reset();
}

/**
 * @brief Tests that writing the same buffer twice by the same node does not create a self-dependency.
 *
 * Resolving a write operation multiple times on the same node should never increment
 * its own dependency degree.
 */
TEST(Unit_DependencyTrackerTest, NoSelfDependency)
{
    DependencyTracker tracker;
    auto node = make_sushi<Node>();

    int buffer = 0;
    void* p = &buffer;

    tracker.resolve(node.get(), p, true);
    tracker.resolve(node.get(), p, true);

    EXPECT_EQ(in_degree_of(node), 0) << "A node must not depend on its own earlier access.";

    tracker.reset();
}
