/**************************************************************************/
/* test_task_graph.cpp                                                    */
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
 * @file test_task_graph.cpp
 * @brief Tests the TaskGraph and the dependency tracker that orders its nodes.
 * @see tests/engine/graph/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/task_work.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>

#include "dependency_tracker.hpp"

using namespace SushiRuntime;

/** @brief Tests what TaskGraph's add calls return or stop on and the errors compile() returns. */
namespace TaskGraphTests
{
    namespace
    {
        using SushiRuntime::ErrorCode;
        using SushiRuntime::BufferRegion;
        using SushiRuntime::Error;
        using SushiRuntime::Internal::Graph::CompiledGraph;
        using SushiRuntime::Internal::Graph::HostWork;
        using SushiRuntime::Internal::Graph::Node;
        using SushiRuntime::Internal::Graph::TaskGraph;
        using SushiRuntime::Internal::Graph::TaskMetadata;
        using SushiRuntime::Internal::Graph::TaskWork;

        /** @brief Returns device work that does nothing; no test submits it. */
        TaskWork idle_work()
        {
            return [](sycl::handler&) {};
        }

        /** @brief Returns host work that does nothing; no test runs it. */
        HostWork idle_host_work()
        {
            return [](sycl::queue&, const std::vector<sycl::event>&) { return sycl::event{}; };
        }

        /** @brief Makes @p later wait on @p earlier, the edge the dependency pass would add. */
        void add_edge(Node& earlier, Node& later)
        {
            earlier.add_successor(&later);
            later.in_degree.fetch_add(1, std::memory_order_relaxed);
        }

        /** @brief Compiles @p graph and returns the error it returns, or nothing for a plan. */
        std::optional<Error> compile_failure(TaskGraph& graph)
        {
            const SushiRuntime::Result<CompiledGraph> compiled = graph.compile();
            if (compiled)
                return std::nullopt;
            return compiled.error();
        }

        /** @brief Tells whether @p text holds @p part. */
        bool contains(const std::string& text, const std::string& part)
        {
            return text.find(part) != std::string::npos;
        }
    } // namespace

    TEST(Unit_TaskGraph, EveryAddReturnsTheNodeItAdded)
    {
        int source = 0;
        int target = 0;
        const std::vector<BufferRegion> reads{BufferRegion(&source)};
        const std::vector<BufferRegion> writes{BufferRegion(&target)};
        TaskMetadata metadata;
        metadata.name = "semantic";

        TaskGraph graph;

        Node& manual = graph.add_node(idle_work());
        EXPECT_EQ(&manual, graph.last_node_added());
        EXPECT_TRUE(manual.io_reads_.empty());

        Node& tracked = graph.add_node(idle_work(), reads, writes);
        EXPECT_EQ(&tracked, graph.last_node_added());
        EXPECT_EQ(tracked.io_writes_.size(), 1u);

        const auto bindings = std::make_shared<SushiRuntime::Internal::Graph::LateBindings>();
        Node& late = graph.add_node(idle_work(), reads, writes, bindings);
        EXPECT_EQ(&late, graph.last_node_added());
        EXPECT_EQ(late.bindings_, bindings);

        Node& device_task = graph.add_task(metadata, reads, writes, idle_work(), idle_host_work());
        EXPECT_EQ(&device_task, graph.last_node_added());
        EXPECT_EQ(device_task.execution_mode(), Node::ExecutionMode::Kernel);

        Node& host_task = graph.add_task(metadata, reads, writes, idle_host_work());
        EXPECT_EQ(&host_task, graph.last_node_added());
        EXPECT_EQ(host_task.execution_mode(), Node::ExecutionMode::Host);

        const auto live_reads = std::make_shared<std::vector<void*>>(1, &source);
        const auto live_writes = std::make_shared<std::vector<void*>>(1, &target);
        Node& dynamic = graph.add_task(metadata, reads, writes, idle_host_work());
        EXPECT_EQ(&dynamic, graph.last_node_added());
        EXPECT_EQ(dynamic.offload_state(), nullptr);
        dynamic.set_live_io(live_reads, live_writes);
        ASSERT_NE(dynamic.offload_state(), nullptr);
        EXPECT_EQ(dynamic.offload_state()->live_reads, live_reads);
        EXPECT_EQ(dynamic.offload_state()->live_writes, live_writes);
        EXPECT_FALSE(dynamic.offload_state()->offloadable);

        Node& native = graph.add_native(reads, writes, [] {});
        EXPECT_EQ(&native, graph.last_node_added());
        EXPECT_FALSE(native.submits_device_work());

        const Node* const added[] = {&manual, &tracked, &late, &device_task,
                                     &host_task, &dynamic, &native};
        for (std::size_t i = 0; i < std::size(added); ++i)
            for (std::size_t j = i + 1; j < std::size(added); ++j)
                EXPECT_NE(added[i], added[j]) << "calls " << i << " and " << j << " share a node";
    }

    TEST(Unit_TaskGraph, AReturnedNodeTakesItsRegistrationAndConfiguration)
    {
        TaskGraph graph;

        const std::uint32_t reserved = graph.reserve_registration();
        Node& stamped = graph.add_node(idle_work(), {}, reserved);
        EXPECT_EQ(stamped.registration_index, reserved);

        Node& drawn = graph.add_node(idle_work());
        EXPECT_EQ(drawn.registration_index, graph.last_node_added()->registration_index);

        // The node is configured through the reference, with no "last node" call.
        drawn.metadata.name = "configured";
        drawn.baked_width = 128;
        drawn.place_on(1);
        EXPECT_EQ(graph.last_node_added()->baked_width, 128u);
        EXPECT_STREQ(graph.last_node_added()->name(), "configured");
        EXPECT_EQ(graph.last_node_added()->stated_placement(), std::optional<std::size_t>(1));
    }

    TEST(Unit_TaskGraphDeathTest, ARegistrationTheGraphNeverIssuedStops)
    {
        TaskGraph graph;

        // An empty graph has issued nothing, and K_NO_REGISTRATION is what it reports as its last.
        EXPECT_DEATH(static_cast<void>(graph.add_node(idle_work(), {}, TaskGraph::K_NO_REGISTRATION)),
                     "TaskGraph::add_node.*registration index 4294967295 was not issued by this "
                     "graph, which has issued 0 so far.*reserve_registration");

        const std::uint32_t reserved = graph.reserve_registration();
        EXPECT_DEATH(static_cast<void>(graph.add_node(idle_work(), {}, reserved + 1)),
                     "has issued 1 so far");
        EXPECT_EQ(graph.last_node_added(), nullptr);
    }

    TEST(Unit_TaskGraphDeathTest, EveryAddOverloadStopsOnAnUnissuedRegistration)
    {
        int source = 0;
        int target = 0;
        const std::vector<BufferRegion> reads{BufferRegion(&source)};
        const std::vector<BufferRegion> writes{BufferRegion(&target)};
        const std::vector<sycl::event> none;
        const std::optional<std::uint32_t> unissued(7);
        TaskMetadata metadata;
        metadata.name = "semantic";

        TaskGraph graph;
        const auto bindings = std::make_shared<SushiRuntime::Internal::Graph::LateBindings>();
        const auto live_reads = std::make_shared<std::vector<void*>>(1, &source);
        const auto live_writes = std::make_shared<std::vector<void*>>(1, &target);
        const char* const pattern = "registration index 7 was not issued by this graph";

        EXPECT_DEATH(static_cast<void>(graph.add_node(idle_work(), reads, writes, none, unissued)),
                     pattern);
        EXPECT_DEATH(static_cast<void>(
                         graph.add_node(idle_work(), reads, writes, bindings, none, unissued)),
                     pattern);
        EXPECT_DEATH(static_cast<void>(graph.add_task(metadata, reads, writes, idle_work(),
                                                      idle_host_work(), none, unissued)),
                     pattern);
        EXPECT_DEATH(static_cast<void>(
                         graph.add_task(metadata, reads, writes, idle_host_work(), none, unissued)),
                     pattern);
        EXPECT_DEATH(static_cast<void>(graph.add_native(reads, writes, [] {}, none, unissued)),
                     pattern);
    }

    TEST(Unit_TaskGraph, AReservedRegistrationIsAcceptedTwice)
    {
        TaskGraph graph;
        const std::uint32_t reserved = graph.reserve_registration();

        Node& first = graph.add_node(idle_work(), {}, reserved);
        Node& second = graph.add_node(idle_work(), {}, reserved);
        EXPECT_EQ(first.registration_index, reserved);
        EXPECT_EQ(second.registration_index, reserved);

        // An index a node drew is issued too, so a later node may share it.
        Node& drawn = graph.add_node(idle_work());
        Node& sharing = graph.add_node(idle_work(), {}, drawn.registration_index);
        EXPECT_EQ(sharing.registration_index, drawn.registration_index);
        EXPECT_NE(drawn.registration_index, reserved);
    }

    TEST(Unit_TaskGraph, TheCompiledPlanOwnsTheReturnedNodes)
    {
        TaskGraph graph;
        Node& first = graph.add_node(idle_work());
        Node& second = graph.add_node(idle_work());

        const CompiledGraph compiled = graph.compile().value();

        ASSERT_EQ(compiled.all_nodes.size(), 2u);
        std::size_t found = 0;
        for (const auto& owned : compiled.all_nodes)
            if (owned.get() == &first || owned.get() == &second)
                ++found;
        EXPECT_EQ(found, 2u);
    }

    TEST(Unit_TaskGraph, CompileWithNoRootReturnsInvalidGraphNamingANode)
    {
        TaskGraph graph;
        Node& first = graph.add_node(idle_work());
        Node& second = graph.add_node(idle_work());
        first.metadata.name = "first";
        second.metadata.name = "second";
        add_edge(first, second);
        add_edge(second, first);

        const std::optional<Error> failure = compile_failure(graph);

        ASSERT_TRUE(failure.has_value()) << "compile() accepted a graph with no root";
        EXPECT_EQ(failure->code, ErrorCode::InvalidGraph);
        EXPECT_TRUE(contains(failure->message, "none of the 2 nodes is a root")) << failure->message;
        EXPECT_TRUE(contains(failure->message, "node 'first' (registration index 0)")
                    || contains(failure->message, "node 'second' (registration index 1)"))
            << failure->message;
    }

    TEST(Unit_TaskGraph, CompileWithACycleBehindARootReturnsInvalidGraphNamingANode)
    {
        TaskGraph graph;
        Node& root = graph.add_node(idle_work());
        Node& first = graph.add_node(idle_work());
        Node& second = graph.add_node(idle_work());
        root.metadata.name = "root";
        first.metadata.name = "first";
        second.metadata.name = "second";
        add_edge(first, second);
        add_edge(second, first);

        const std::optional<Error> failure = compile_failure(graph);

        ASSERT_TRUE(failure.has_value()) << "compile() accepted a graph with a cycle";
        EXPECT_EQ(failure->code, ErrorCode::InvalidGraph);
        EXPECT_TRUE(contains(failure->message, "2 of 3 nodes")) << failure->message;
        EXPECT_TRUE(contains(failure->message, "node 'first' (registration index 1)")
                    || contains(failure->message, "node 'second' (registration index 2)"))
            << failure->message;
        EXPECT_FALSE(contains(failure->message, "node 'root'")) << failure->message;
    }

    TEST(Unit_TaskGraph, AGraphCompilesAfterAFailedCompile)
    {
        TaskGraph graph;
        Node& first = graph.add_node(idle_work());
        Node& second = graph.add_node(idle_work());
        add_edge(first, second);
        add_edge(second, first);
        ASSERT_TRUE(compile_failure(graph).has_value());

        graph.add_node(idle_work());
        const CompiledGraph compiled = graph.compile().value();
        EXPECT_EQ(compiled.all_nodes.size(), 1u);
        EXPECT_EQ(compiled.roots.size(), 1u);
    }
} // namespace TaskGraphTests

/** @brief Tests the edges the lock-free DependencyTracker creates between raw nodes. */
namespace DependencyTrackerTests
{
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

        tracker.resolve(writer.get(), SushiRuntime::BufferRegion(p), true);
        tracker.resolve(reader.get(), SushiRuntime::BufferRegion(p), false);

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

        tracker.resolve(w1.get(), SushiRuntime::BufferRegion(p), true);   // initial write
        tracker.resolve(rd.get(), SushiRuntime::BufferRegion(p), false);  // RAW on w1
        tracker.resolve(w2.get(), SushiRuntime::BufferRegion(p), true);   // WAR on rd + WAW on w1

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

        tracker.resolve(writer.get(), SushiRuntime::BufferRegion(p), true);
        tracker.resolve(r1.get(), SushiRuntime::BufferRegion(p), false);
        tracker.resolve(r2.get(), SushiRuntime::BufferRegion(p), false);

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

        tracker.resolve(writer.get(), SushiRuntime::BufferRegion(pa), true);
        tracker.resolve(writer.get(), SushiRuntime::BufferRegion(pb), true);
        tracker.resolve(reader.get(), SushiRuntime::BufferRegion(pa), false); // edge writer -> reader
        tracker.resolve(reader.get(), SushiRuntime::BufferRegion(pb), false); // same edge again -> must dedup

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

        tracker.resolve(node.get(), SushiRuntime::BufferRegion(p), true);
        tracker.resolve(node.get(), SushiRuntime::BufferRegion(p), true);

        EXPECT_EQ(in_degree_of(node), 0) << "A node must not depend on its own earlier access.";

        tracker.reset();
    }

    using SushiRuntime::BufferRegion;

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
        tracker.resolve(w1.get(), BufferRegion{base, 0, 32}, true);
        tracker.resolve(w2.get(), BufferRegion{base, 32, 32}, true);

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
        tracker.resolve(w1.get(), BufferRegion{base, 0, 40}, true);
        tracker.resolve(w2.get(), BufferRegion{base, 32, 32}, true);

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
        tracker.resolve(whole.get(), SushiRuntime::BufferRegion(base), true);                 // whole-buffer write
        tracker.resolve(part.get(), BufferRegion{base, 16, 8}, true);

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
        tracker.resolve(w_lo.get(), BufferRegion{base, 0, 32}, true);
        tracker.resolve(w_hi.get(), BufferRegion{base, 32, 32}, true);
        tracker.resolve(rd.get(),   BufferRegion{base, 0, 64}, false);

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
        tracker.resolve(rd.get(), BufferRegion{base, 0, 16}, false);
        tracker.resolve(wp.get(), BufferRegion{base, 16, 16}, true);
        tracker.resolve(w.get(),  BufferRegion{base, 0, 32}, true);

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
        tracker.resolve(w1.get(), BufferRegion{base_a, 0, 32}, true);
        tracker.resolve(w2.get(), BufferRegion{base_b, 0, 32}, true);

        EXPECT_EQ(in_degree_of(w2), 0)
            << "Writes to different allocations must never be ordered.";

        tracker.reset();
    }
} // namespace DependencyTrackerTests
