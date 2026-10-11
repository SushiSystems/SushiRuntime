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
 * @brief Tests what TaskGraph's add calls return or refuse and the errors compile() throws.
 * @see modules/engine/graph/tests/README.md
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
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/task_work.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>

namespace
{
    using SushiRuntime::Core::Errc;
    using SushiRuntime::Memory::ResourceRegion;
    using SushiRuntime::Core::SushiException;
    using SushiRuntime::Graph::CompiledGraph;
    using SushiRuntime::Graph::HostWork;
    using SushiRuntime::Graph::Node;
    using SushiRuntime::Graph::TaskGraph;
    using SushiRuntime::Graph::TaskMetadata;
    using SushiRuntime::Graph::TaskWork;

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

    /** @brief Pairs the code and the message of an error the graph threw. */
    struct GraphFailure
    {
        Errc code = Errc::ok;  /**< The code of the SushiException. */
        std::string message;   /**< Its what(). */
    };

    /** @brief Compiles @p graph and returns the SushiException it throws, or nothing. */
    std::optional<GraphFailure> compile_failure(TaskGraph& graph)
    {
        try
        {
            const CompiledGraph compiled = graph.compile();
        }
        catch (const SushiException& failure)
        {
            return GraphFailure{failure.error().code, failure.what()};
        }
        return std::nullopt;
    }

    /** @brief Calls @p add and returns the SushiException it throws, or nothing. */
    template <class Add>
    std::optional<GraphFailure> add_failure(Add&& add)
    {
        try
        {
            add();
        }
        catch (const SushiException& failure)
        {
            return GraphFailure{failure.error().code, failure.what()};
        }
        return std::nullopt;
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
    const std::vector<ResourceRegion> reads{ResourceRegion(&source)};
    const std::vector<ResourceRegion> writes{ResourceRegion(&target)};
    TaskMetadata metadata;
    metadata.name = "semantic";

    TaskGraph graph;

    Node& manual = graph.add_node(idle_work());
    EXPECT_EQ(&manual, graph.last_node_added());
    EXPECT_TRUE(manual.io_reads_.empty());

    Node& tracked = graph.add_node(idle_work(), reads, writes);
    EXPECT_EQ(&tracked, graph.last_node_added());
    EXPECT_EQ(tracked.io_writes_.size(), 1u);

    const auto bindings = std::make_shared<SushiRuntime::Graph::LateBindings>();
    Node& late = graph.add_node(idle_work(), reads, writes, bindings);
    EXPECT_EQ(&late, graph.last_node_added());
    EXPECT_EQ(late.bindings_, bindings);

    Node& device_task = graph.add_task(metadata, reads, writes, idle_work(), idle_host_work());
    EXPECT_EQ(&device_task, graph.last_node_added());
    EXPECT_EQ(device_task.get_execution_mode(), Node::ExecutionMode::KERNEL);

    Node& host_task = graph.add_task(metadata, reads, writes, idle_host_work());
    EXPECT_EQ(&host_task, graph.last_node_added());
    EXPECT_EQ(host_task.get_execution_mode(), Node::ExecutionMode::HOST);

    const auto live_reads = std::make_shared<std::vector<void*>>(1, &source);
    const auto live_writes = std::make_shared<std::vector<void*>>(1, &target);
    Node& dynamic = graph.add_offload_dynamic(metadata, live_reads, live_writes, idle_host_work());
    EXPECT_EQ(&dynamic, graph.last_node_added());
    ASSERT_NE(dynamic.offload_state(), nullptr);
    EXPECT_EQ(dynamic.offload_state()->live_writes, live_writes);

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
    EXPECT_STREQ(graph.last_node_added()->get_name(), "configured");
    EXPECT_EQ(graph.last_node_added()->stated_placement(), std::optional<std::size_t>(1));
}

TEST(Unit_TaskGraph, ARegistrationTheGraphNeverIssuedIsRefused)
{
    TaskGraph graph;

    // An empty graph has issued nothing, and NO_REGISTRATION is what it reports as its last.
    const std::optional<GraphFailure> sentinel = add_failure(
        [&graph] { graph.add_node(idle_work(), {}, TaskGraph::NO_REGISTRATION); });
    ASSERT_TRUE(sentinel.has_value()) << "add_node() accepted NO_REGISTRATION";
    EXPECT_EQ(sentinel->code, Errc::invalid_graph);
    EXPECT_TRUE(contains(sentinel->message, "TaskGraph::add_node()")) << sentinel->message;
    EXPECT_TRUE(contains(sentinel->message, "registration index 4294967295"))
        << sentinel->message;
    EXPECT_TRUE(contains(sentinel->message, "has issued 0 so far")) << sentinel->message;
    EXPECT_TRUE(contains(sentinel->message, "reserve_registration()")) << sentinel->message;

    const std::uint32_t reserved = graph.reserve_registration();
    const std::optional<GraphFailure> past = add_failure(
        [&graph, reserved] { graph.add_node(idle_work(), {}, reserved + 1); });
    ASSERT_TRUE(past.has_value()) << "add_node() accepted an index one past the last issued";
    EXPECT_EQ(past->code, Errc::invalid_graph);
    EXPECT_TRUE(contains(past->message, "has issued 1 so far")) << past->message;

    EXPECT_EQ(graph.last_node_added(), nullptr) << "a refused call added a node";
}

TEST(Unit_TaskGraph, EveryAddOverloadRefusesAnUnissuedRegistration)
{
    int source = 0;
    int target = 0;
    const std::vector<ResourceRegion> reads{ResourceRegion(&source)};
    const std::vector<ResourceRegion> writes{ResourceRegion(&target)};
    const std::vector<sycl::event> none;
    const std::optional<std::uint32_t> unissued(7);
    TaskMetadata metadata;
    metadata.name = "semantic";

    TaskGraph graph;
    const auto bindings = std::make_shared<SushiRuntime::Graph::LateBindings>();
    const auto live_reads = std::make_shared<std::vector<void*>>(1, &source);
    const auto live_writes = std::make_shared<std::vector<void*>>(1, &target);

    const std::optional<GraphFailure> refused[] = {
        add_failure([&] { graph.add_node(idle_work(), reads, writes, none, unissued); }),
        add_failure([&] { graph.add_node(idle_work(), reads, writes, bindings, none, unissued); }),
        add_failure([&] {
            graph.add_task(metadata, reads, writes, idle_work(), idle_host_work(), none,
                           unissued);
        }),
        add_failure([&] {
            graph.add_task(metadata, reads, writes, idle_host_work(), none, unissued);
        }),
        add_failure([&] {
            graph.add_offload_dynamic(metadata, live_reads, live_writes, idle_host_work(), none,
                                      unissued);
        }),
        add_failure([&] { graph.add_native(reads, writes, [] {}, none, unissued); }),
    };
    for (std::size_t i = 0; i < std::size(refused); ++i)
    {
        ASSERT_TRUE(refused[i].has_value()) << "overload " << i << " accepted the index";
        EXPECT_EQ(refused[i]->code, Errc::invalid_graph) << "overload " << i;
    }

    // A refused call left no access behind: the next writer of target is a root.
    Node& writer = graph.add_node(idle_work(), reads, writes);
    EXPECT_EQ(writer.in_degree.load(std::memory_order_relaxed), 0);
    const CompiledGraph compiled = graph.compile();
    EXPECT_EQ(compiled.all_nodes.size(), 1u);
    EXPECT_EQ(compiled.roots.size(), 1u);
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

    const CompiledGraph compiled = graph.compile();

    ASSERT_EQ(compiled.all_nodes.size(), 2u);
    std::size_t found = 0;
    for (const auto& owned : compiled.all_nodes)
        if (owned.get() == &first || owned.get() == &second)
            ++found;
    EXPECT_EQ(found, 2u);
}

TEST(Unit_TaskGraph, CompileWithNoRootThrowsInvalidGraphNamingANode)
{
    TaskGraph graph;
    Node& first = graph.add_node(idle_work());
    Node& second = graph.add_node(idle_work());
    first.metadata.name = "first";
    second.metadata.name = "second";
    add_edge(first, second);
    add_edge(second, first);

    const std::optional<GraphFailure> failure = compile_failure(graph);

    ASSERT_TRUE(failure.has_value()) << "compile() accepted a graph with no root";
    EXPECT_EQ(failure->code, Errc::invalid_graph);
    EXPECT_TRUE(contains(failure->message, "none of the 2 nodes is a root")) << failure->message;
    EXPECT_TRUE(contains(failure->message, "node 'first' (registration index 0)")
                || contains(failure->message, "node 'second' (registration index 1)"))
        << failure->message;
}

TEST(Unit_TaskGraph, CompileWithACycleBehindARootThrowsInvalidGraphNamingANode)
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

    const std::optional<GraphFailure> failure = compile_failure(graph);

    ASSERT_TRUE(failure.has_value()) << "compile() accepted a graph with a cycle";
    EXPECT_EQ(failure->code, Errc::invalid_graph);
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
    const CompiledGraph compiled = graph.compile();
    EXPECT_EQ(compiled.all_nodes.size(), 1u);
    EXPECT_EQ(compiled.roots.size(), 1u);
}
