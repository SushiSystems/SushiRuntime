/**************************************************************************/
/* test_dependency_tracker.cpp                                          */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

/**
 * @file test_dependency_tracker.cpp
 * @brief Unit coverage for the lock-free DependencyTracker hazard resolution.
 *
 * The integration tests prove the tracker serializes a real graph; these unit
 * tests pin the exact edge semantics on raw nodes, deterministically and with
 * no hardware:
 *   - RAW / WAR / WAW each create the right edge (in_degree increments),
 *   - two readers of one buffer depend on the writer but NOT on each other
 *     (RAR must stay parallel — no false serialization between reads),
 *   - sharing several buffers between the same pair adds only ONE edge
 *     (dependency dedup, guards the duplicate-link fix),
 *   - a node accessing its own buffer twice never depends on itself.
 *
 * in_degree is the observable: it is the count of incoming edges the scheduler
 * decrements before a node becomes runnable.
 */

#include <gtest/gtest.h>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/graph/dependency_tracker.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Graph;

namespace
{
    int in_degree_of(const sushi_ptr<Node>& n)
    {
        return n->in_degree.load(std::memory_order_relaxed);
    }
}

/**
 * @test RawCreatesEdge
 * @brief writer(W) then reader(R) on the same buffer: the reader gains one
 * incoming edge (read-after-write), the writer none.
 */
TEST(Unit_DependencyTrackerTest, RawCreatesEdge)
{
    DependencyTracker tracker;
    auto writer = make_sushi<Node>();
    auto reader = make_sushi<Node>();

    int buffer = 0;
    void* p = &buffer;

    tracker.resolve(writer.get(), p, /*is_write=*/true);
    tracker.resolve(reader.get(), p, /*is_write=*/false);

    EXPECT_EQ(in_degree_of(writer), 0);
    EXPECT_EQ(in_degree_of(reader), 1) << "Reader must depend on the preceding writer (RAW).";

    tracker.reset();
}

/**
 * @test WarAndWawChain
 * @brief W1 -> R -> W2 on one buffer. R depends on W1 (RAW). W2 depends on BOTH
 * R (write-after-read) and W1 (write-after-write), so its in_degree is 2.
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
 * @test ReadersDependOnWriterNotEachOther
 * @brief One writer followed by two readers: both readers depend on the writer,
 * but NOT on each other, so they can run concurrently. A naive tracker that
 * serialized reads would give the second reader in_degree 2.
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
 * @test SharedBuffersCreateSingleEdge
 * @brief A writer and a reader that share TWO buffers must be linked by only one
 * dependency edge, not one per shared buffer (duplicate-link dedup).
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
 * @test NoSelfDependency
 * @brief A node that writes the same buffer twice must never create an edge to
 * itself (which would deadlock it: it could never reach in_degree 0).
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
