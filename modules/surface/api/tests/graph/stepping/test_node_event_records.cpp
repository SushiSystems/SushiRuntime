/**************************************************************************/
/* test_node_event_records.cpp                                            */
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
 * @file test_node_event_records.cpp
 * @brief Tests per-dispatch node event records and their pairing with the event clock.
 * @see modules/surface/api/tests/graph/stepping/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/task/node.hpp>

#include "plan_access.hpp"

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief Declares the test-only friend access to the plan of a Graph. */
        struct NodeEventRecordsTestAccess
        {
            static const SushiRuntime::Internal::Graph::CompiledGraph& plan(const Graph& g) noexcept
            {
                return *engine_plan(g.compiled_plan());
            }
        };
    }
}

using namespace SushiRuntime;

namespace
{
    // Registration index of the one compiled node named @p name.
    std::uint32_t registration_index_of(const Internal::Graph::CompiledGraph& plan, const char* name)
    {
        const Internal::Graph::Node* found = nullptr;
        for (const auto& n : plan.all_nodes)
        {
            if (std::string(n->metadata.name) == name)
            {
                EXPECT_EQ(found, nullptr) << "Node name '" << name << "' is not unique.";
                found = n.get();
            }
        }
        EXPECT_NE(found, nullptr) << "No compiled node named '" << name << "'.";
        return found ? found->registration_index : 0u;
    }
} // namespace

// E.1 - a graph that never asked for profiling must not leak any record.
TEST(Integration_NodeEventRecords, ProfilingNotRequestedLeavesNodeEventsEmpty)
{
    constexpr std::size_t N = 128;

    auto rt = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();
    int* p = buf.data();

    auto g = rt.graph();
    EXPECT_FALSE(g.is_profiling_requested());
    g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });

    auto report = g.run().value();
    EXPECT_TRUE(report.node_events.empty());
    EXPECT_EQ(report.clock.event_ns, 0u);
    EXPECT_EQ(report.clock.steady_ns, 0u);
}

/**
 * @brief Tests that three kernel nodes over run(5) give fifteen records with submit <= start <=
 *        end.
 */
TEST(Integration_NodeEventRecords, RequestedGraphRecordsOnePerDispatch)
{
    constexpr std::size_t N = 64;

    auto rt = Runtime::create().value();
    auto a = rt.buffer<int>(N).value();
    auto b = rt.buffer<int>(N).value();
    auto c = rt.buffer<int>(N).value();
    int* pa = a.data();
    int* pb = b.data();
    int* pc = c.data();

    auto g = rt.graph();
    g.set_profiling(true);
    g.add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] = static_cast<int>(i); });
    g.add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] = static_cast<int>(i); });
    g.add(N, Reads{}, Writes(c), [pc](std::size_t i) { pc[i] = static_cast<int>(i); });

    auto report = g.run(std::size_t{5}).value();
    ASSERT_EQ(report.node_events.size(), 15u);

    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> invocations_by_node;
    for (const auto& rec : report.node_events)
    {
        EXPECT_LE(rec.submit_ns, rec.start_ns);
        EXPECT_LE(rec.start_ns, rec.end_ns);
        invocations_by_node[rec.node_index].push_back(rec.invocation);
    }

    ASSERT_EQ(invocations_by_node.size(), 3u) << "expected one row per kernel node";
    for (auto& [node_index, invocations] : invocations_by_node)
    {
        std::sort(invocations.begin(), invocations.end());
        ASSERT_EQ(invocations.size(), 5u) << "node " << node_index;
        for (std::uint32_t i = 0; i < 5; ++i)
            EXPECT_EQ(invocations[i], i) << "node " << node_index;
    }
}

// The steady-state capture case is in test_steady_state_allocation.cpp; see README.md.

/** @brief Tests that node_index follows registration order, not the compiled order. */
TEST(Integration_NodeEventRecords, NodeIndexIsRegistrationOrder)
{
    constexpr std::size_t N = 32;

    auto rt = Runtime::create().value();
    auto a = rt.buffer<int>(N).value();
    auto b = rt.buffer<int>(N).value();
    auto c = rt.buffer<int>(N).value();
    int* pa = a.data();
    int* pb = b.data();
    int* pc = c.data();

    // The strict dependency chain forces the execution order A, B, C; see README.md.
    auto g = rt.graph();
    g.set_profiling(true);
    g.add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name("A");
    g.add(N, Reads(a), Writes(b), [pa, pb](std::size_t i) { pb[i] = pa[i] + 1; }).name("B");
    g.add(N, Reads(b), Writes(c), [pb, pc](std::size_t i) { pc[i] = pb[i] + 1; }).name("C");

    auto report = g.run().value();

    const auto& plan = Detail::NodeEventRecordsTestAccess::plan(g);
    const std::uint32_t idx_a = registration_index_of(plan, "A");
    const std::uint32_t idx_b = registration_index_of(plan, "B");
    const std::uint32_t idx_c = registration_index_of(plan, "C");

    EXPECT_EQ(idx_a, 0u);
    EXPECT_EQ(idx_b, 1u);
    EXPECT_EQ(idx_c, 2u);

    std::unordered_map<std::uint32_t, std::uint64_t> earliest_submit;
    for (const auto& rec : report.node_events)
    {
        auto [it, inserted] = earliest_submit.try_emplace(rec.node_index, rec.submit_ns);
        if (!inserted) it->second = std::min(it->second, rec.submit_ns);
    }
    ASSERT_EQ(earliest_submit.count(idx_a), 1u);
    ASSERT_EQ(earliest_submit.count(idx_b), 1u);
    ASSERT_EQ(earliest_submit.count(idx_c), 1u);

    EXPECT_LE(earliest_submit[idx_a], earliest_submit[idx_b])
        << "A's dependency chain must have it dispatch no later than B";
    EXPECT_LE(earliest_submit[idx_b], earliest_submit[idx_c])
        << "B's dependency chain must have it dispatch no later than C";
}

/** @brief Tests that a host node's converted record lands inside the run's event window. */
TEST(Integration_NodeEventRecords, HostNodeLandsOnEventClock)
{
    constexpr std::size_t N = 64;

    auto rt = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();
    int* p = buf.data();

    // The host node reads what the kernel writes, so it runs after the kernel; see README.md.
    auto g = rt.graph();
    g.set_profiling(true);
    g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); }).name("kernel");
    g.add_host(Reads(buf), Writes{}, [p]()
    {
        volatile int sink = 0;
        for (std::size_t i = 0; i < N; ++i) sink += p[i];
        (void)sink;
    });

    auto report = g.run().value();
    ASSERT_FALSE(report.node_events.empty());

    const auto& plan = Detail::NodeEventRecordsTestAccess::plan(g);
    const std::uint32_t idx_kernel = registration_index_of(plan, "kernel");

    // Only a host record has submit_ns equal to start_ns; see README.md.
    const NodeEventRecord* kernel_record = nullptr;
    const NodeEventRecord* host_record = nullptr;
    for (const auto& rec : report.node_events)
    {
        if (rec.node_index == idx_kernel) kernel_record = &rec;
        else if (rec.submit_ns == rec.start_ns) host_record = &rec;
    }

    ASSERT_NE(kernel_record, nullptr) << "no kernel record found among node_events";
    ASSERT_NE(host_record, nullptr) << "no host record found among node_events";

    // The tolerance covers the clock pairing's measurement uncertainty only; see README.md.
    EXPECT_GE(host_record->start_ns + report.clock.uncertainty_ns, kernel_record->end_ns)
        << "the host record must not land before the kernel it depends on completed";
    EXPECT_LE(host_record->start_ns, host_record->end_ns);
}
