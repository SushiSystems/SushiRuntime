/**************************************************************************/
/* test_dependency_tracker.cpp                                            */
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
 * @file test_dependency_tracker.cpp
 * @brief Tests the edges the lock-free DependencyTracker creates between raw nodes.
 * @see modules/engine/graph/tests/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>
#include <SushiRuntime/task/node.hpp>
#include "dependency_tracker.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

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
 * @brief Tests that writing the same buffer twice by the same node does not create a
 *        self-dependency.
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

using SushiRuntime::Internal::Memory::ResourceRegion;

/**
 * @brief Tests that writes to disjoint sub-regions of one allocation stay parallel.
 *
 * Two writers touching non-overlapping byte ranges of the same base must not be
 * ordered — the core win of region identity over base-pointer identity.
 */
TEST(Unit_DependencyTrackerTest, DisjointSubRegionsAreParallel)
{
    DependencyTracker tracker;
    auto w1 = make_sushi<Node>();
    auto w2 = make_sushi<Node>();

    char base[64];
    tracker.resolve(w1.get(), ResourceRegion{base, 0, 32}, true);
    tracker.resolve(w2.get(), ResourceRegion{base, 32, 32}, true);

    EXPECT_EQ(in_degree_of(w2), 0)
        << "Writes to disjoint halves of one buffer must not serialize.";

    tracker.reset();
}

/**
 * @brief Tests that writes to overlapping sub-regions are ordered (WAW).
 *
 * Ranges [0,40) and [32,64) share bytes [32,40), so the second writer must
 * depend on the first.
 */
TEST(Unit_DependencyTrackerTest, OverlappingSubRegionsAreOrdered)
{
    DependencyTracker tracker;
    auto w1 = make_sushi<Node>();
    auto w2 = make_sushi<Node>();

    char base[64];
    tracker.resolve(w1.get(), ResourceRegion{base, 0, 40}, true);
    tracker.resolve(w2.get(), ResourceRegion{base, 32, 32}, true);

    EXPECT_EQ(in_degree_of(w2), 1) << "Overlapping writes (WAW) must be ordered.";

    tracker.reset();
}

/**
 * @brief Tests that a whole-buffer access conflicts with any sub-region of it.
 *
 * A bare base pointer is the whole-buffer region, which contains every
 * sub-region of the same allocation, so it serializes against them.
 */
TEST(Unit_DependencyTrackerTest, WholeBufferConflictsWithSubRegion)
{
    DependencyTracker tracker;
    auto whole = make_sushi<Node>();
    auto part  = make_sushi<Node>();

    char base[64];
    tracker.resolve(whole.get(), base, true);                 // whole-buffer write
    tracker.resolve(part.get(), ResourceRegion{base, 16, 8}, true);

    EXPECT_EQ(in_degree_of(part), 1)
        << "A sub-region write must order after a prior whole-buffer write.";

    tracker.reset();
}

/**
 * @brief Tests that a read over a span depends on every overlapping prior write.
 *
 * A read of [0,64) follows two writers that each cover one half; stopping at the
 * first writer found would drop one RAW edge. The read must depend on both.
 */
TEST(Unit_DependencyTrackerTest, ReadDependsOnAllOverlappingWrites)
{
    DependencyTracker tracker;
    auto w_lo = make_sushi<Node>();
    auto w_hi = make_sushi<Node>();
    auto rd   = make_sushi<Node>();

    char base[64];
    tracker.resolve(w_lo.get(), ResourceRegion{base, 0, 32}, true);
    tracker.resolve(w_hi.get(), ResourceRegion{base, 32, 32}, true);
    tracker.resolve(rd.get(),   ResourceRegion{base, 0, 64}, false);

    EXPECT_EQ(in_degree_of(rd), 2)
        << "A read spanning both halves must depend on both half-writers (RAW).";

    tracker.reset();
}

/**
 * @brief Tests that the write walk does not stop at a partially-covering writer.
 *
 * History: a read of [0,16), then a write of [16,32), then a write of [0,32). The
 * middle writer overlaps the final write but does not cover its [0,16) part, and
 * it does not depend on the earlier read. Stopping the walk there would drop the
 * WAR edge to the read and silently race. The final write must depend on both.
 */
TEST(Unit_DependencyTrackerTest, WriteWalkContinuesPastPartialWriter)
{
    DependencyTracker tracker;
    auto rd = make_sushi<Node>();
    auto wp = make_sushi<Node>();
    auto w  = make_sushi<Node>();

    char base[64];
    tracker.resolve(rd.get(), ResourceRegion{base, 0, 16}, false);
    tracker.resolve(wp.get(), ResourceRegion{base, 16, 16}, true);
    tracker.resolve(w.get(),  ResourceRegion{base, 0, 32}, true);

    EXPECT_EQ(in_degree_of(wp), 0)
        << "The partial writer must not depend on the disjoint earlier read.";
    EXPECT_EQ(in_degree_of(w), 2)
        << "The covering write must order after both the partial writer and the read.";

    tracker.reset();
}

/**
 * @brief Tests that distinct allocations never serialize.
 *
 * Two writers to different base allocations must stay parallel even if their
 * byte offsets coincide — overlap requires the same base.
 */
TEST(Unit_DependencyTrackerTest, DistinctAllocationsAreParallel)
{
    DependencyTracker tracker;
    auto w1 = make_sushi<Node>();
    auto w2 = make_sushi<Node>();

    char base_a[64];
    char base_b[64];
    tracker.resolve(w1.get(), ResourceRegion{base_a, 0, 32}, true);
    tracker.resolve(w2.get(), ResourceRegion{base_b, 0, 32}, true);

    EXPECT_EQ(in_degree_of(w2), 0)
        << "Writes to different allocations must never be ordered.";

    tracker.reset();
}
