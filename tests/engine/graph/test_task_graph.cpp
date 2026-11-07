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
 * @file test_task_graph.cpp
 * @brief Tests the TaskGraph and the dependency tracker that orders its nodes.
 * @see tests/engine/graph/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
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

        /** @brief Two integers and the region lists that read the first and write the second. */
        struct Buffers
        {
            int source = 0;
            int target = 0;

            /** @brief Returns the region list that reads the source. */
            std::vector<BufferRegion> reads() { return {BufferRegion(&source)}; }

            /** @brief Returns the region list that writes the target. */
            std::vector<BufferRegion> writes() { return {BufferRegion(&target)}; }
        };

        /** @brief Returns the metadata every task-kind test gives its tasks. */
        TaskMetadata named_metadata()
        {
            TaskMetadata metadata;
            metadata.name = "semantic";
            return metadata;
        }

        /** @brief Returns fresh late bindings. */
        std::shared_ptr<SushiRuntime::Internal::Graph::LateBindings> late_bindings()
        {
            return std::make_shared<SushiRuntime::Internal::Graph::LateBindings>();
        }

        /** @brief Tells whether @p text holds @p part. */
        bool contains(const std::string& text, const std::string& part)
        {
            return text.find(part) != std::string::npos;
        }
    } // namespace

    /** @brief Tests that every add call returns the node last_node_added() reports, and no two calls share one. */
    TEST(Small_TaskGraph, EveryAddReturnsTheNodeItAdded)
    {
        Buffers buffers;
        const TaskMetadata metadata = named_metadata();
        TaskGraph graph;

        const Node& manual = graph.add_node(idle_work());
        EXPECT_EQ(&manual, graph.last_node_added());
        const Node& tracked = graph.add_node(idle_work(), buffers.reads(), buffers.writes());
        EXPECT_EQ(&tracked, graph.last_node_added());
        const Node& late = graph.add_node(idle_work(), buffers.reads(), buffers.writes(), late_bindings());
        EXPECT_EQ(&late, graph.last_node_added());
        const Node& device_task =
            graph.add_task(metadata, buffers.reads(), buffers.writes(), idle_work(), idle_host_work());
        EXPECT_EQ(&device_task, graph.last_node_added());
        const Node& host_task = graph.add_task(metadata, buffers.reads(), buffers.writes(), idle_host_work());
        EXPECT_EQ(&host_task, graph.last_node_added());
        const Node& dynamic = graph.add_task(metadata, buffers.reads(), buffers.writes(), idle_host_work());
        EXPECT_EQ(&dynamic, graph.last_node_added());
        const Node& native = graph.add_native(buffers.reads(), buffers.writes(), [] {});
        EXPECT_EQ(&native, graph.last_node_added());

        const std::set<const Node*> distinct{&manual, &tracked, &late, &device_task, &host_task, &dynamic, &native};
        EXPECT_EQ(distinct.size(), 7u) << "two add calls share a node";
    }

    /** @brief Tests that a node added with regions carries them, and one added without carries none. */
    TEST(Small_TaskGraph, AddedRegionsLandOnTheNode)
    {
        Buffers buffers;
        TaskGraph graph;

        const Node& manual = graph.add_node(idle_work());
        const Node& tracked = graph.add_node(idle_work(), buffers.reads(), buffers.writes());

        EXPECT_TRUE(manual.io_reads_.empty());
        EXPECT_EQ(tracked.io_writes_.size(), 1u);
    }

    /** @brief Tests that the late bindings given to an add call are the ones the node holds. */
    TEST(Small_TaskGraph, LateBindingsLandOnTheNode)
    {
        Buffers buffers;
        const auto bindings = late_bindings();
        TaskGraph graph;

        const Node& late = graph.add_node(idle_work(), buffers.reads(), buffers.writes(), bindings);

        EXPECT_EQ(late.bindings_, bindings);
    }

    /** @brief Tests that a task with device work runs as a kernel and a task with only host work as a host body. */
    TEST(Small_TaskGraph, TheExecutionModeFollowsTheTaskKind)
    {
        Buffers buffers;
        const TaskMetadata metadata = named_metadata();
        TaskGraph graph;

        const Node& device_task =
            graph.add_task(metadata, buffers.reads(), buffers.writes(), idle_work(), idle_host_work());
        const Node& host_task = graph.add_task(metadata, buffers.reads(), buffers.writes(), idle_host_work());

        EXPECT_EQ(device_task.execution_mode(), Node::ExecutionMode::Kernel);
        EXPECT_EQ(host_task.execution_mode(), Node::ExecutionMode::Host);
    }

    /** @brief Tests that live io set on a task node lands in its offload state, which a fresh node lacks. */
    TEST(Small_TaskGraph, LiveIoLandsInTheOffloadState)
    {
        Buffers buffers;
        const auto live_reads = std::make_shared<std::vector<void*>>(1, &buffers.source);
        const auto live_writes = std::make_shared<std::vector<void*>>(1, &buffers.target);
        TaskGraph graph;
        Node& dynamic = graph.add_task(named_metadata(), buffers.reads(), buffers.writes(), idle_host_work());
        ASSERT_EQ(dynamic.offload_state(), nullptr);

        dynamic.set_live_io(live_reads, live_writes);

        ASSERT_NE(dynamic.offload_state(), nullptr);
        EXPECT_EQ(dynamic.offload_state()->live_reads, live_reads);
        EXPECT_EQ(dynamic.offload_state()->live_writes, live_writes);
        EXPECT_FALSE(dynamic.offload_state()->offloadable);
    }

    /** @brief Tests that a native node submits no device work. */
    TEST(Small_TaskGraph, ANativeNodeSubmitsNoDeviceWork)
    {
        Buffers buffers;
        TaskGraph graph;

        const Node& native = graph.add_native(buffers.reads(), buffers.writes(), [] {});

        EXPECT_FALSE(native.submits_device_work());
    }

    /** @brief Tests that a reserved registration is stamped on the node, and a node added without one draws its own. */
    TEST(Small_TaskGraph, AReturnedNodeTakesItsRegistration)
    {
        TaskGraph graph;

        const std::uint32_t reserved = graph.reserve_registration();
        Node& stamped = graph.add_node(idle_work(), {}, reserved);
        EXPECT_EQ(stamped.registration_index, reserved);

        Node& drawn = graph.add_node(idle_work());
        EXPECT_EQ(drawn.registration_index, graph.last_node_added()->registration_index);
    }

    TEST(Medium_TaskGraphDeathTest, ARegistrationTheGraphNeverIssuedStops)
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

    TEST(Medium_TaskGraphDeathTest, EveryAddOverloadStopsOnAnUnissuedRegistration)
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

    /** @brief Tests that a reserved registration may be given to two nodes. */
    TEST(Small_TaskGraph, AReservedRegistrationIsAcceptedTwice)
    {
        TaskGraph graph;
        const std::uint32_t reserved = graph.reserve_registration();

        Node& first = graph.add_node(idle_work(), {}, reserved);
        Node& second = graph.add_node(idle_work(), {}, reserved);

        EXPECT_EQ(first.registration_index, reserved);
        EXPECT_EQ(second.registration_index, reserved);
    }

    /** @brief Tests that an index a node drew may be given to a later node. */
    TEST(Small_TaskGraph, AnIndexANodeDrewMayBeShared)
    {
        TaskGraph graph;
        const std::uint32_t reserved = graph.reserve_registration();

        Node& drawn = graph.add_node(idle_work());
        Node& sharing = graph.add_node(idle_work(), {}, drawn.registration_index);

        EXPECT_EQ(sharing.registration_index, drawn.registration_index);
        EXPECT_NE(drawn.registration_index, reserved);
    }

    TEST(Small_TaskGraph, TheCompiledPlanOwnsTheReturnedNodes)
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

    TEST(Small_TaskGraph, CompileWithNoRootReturnsInvalidGraphNamingANode)
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

    TEST(Small_TaskGraph, CompileWithACycleBehindARootReturnsInvalidGraphNamingANode)
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

    TEST(Small_TaskGraph, AGraphCompilesAfterAFailedCompile)
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
