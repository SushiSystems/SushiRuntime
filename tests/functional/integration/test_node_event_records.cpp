/**************************************************************************/
/* test_node_event_records.cpp                                           */
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

// G1 task 5 coverage: per-dispatch node event records, the clock pairing, and
// steady-state capture (spec
// docs/superpowers/specs/2026-09-15-g1-node-event-records-design.md §C, §E
// items 1, 2, 3, 4, 6; plan task 5).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            // Test-only friend seam onto Graph::plan_, mirroring
            // test_registration_index.cpp's RegistrationIndexTestAccess.
            struct NodeEventRecordsTestAccess
            {
                static const Execution::CompiledGraph& plan(const Graph& g) noexcept
                {
                    return g.plan_;
                }
            };
        }
    }
}

using namespace SushiRuntime;

namespace
{
    // Registration index of the one compiled node named @p name.
    std::uint32_t registration_index_of(const Execution::CompiledGraph& plan, const char* name)
    {
        const Graph::Node* found = nullptr;
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

    auto rt = Runtime::create();
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    EXPECT_FALSE(g.profiling_requested());
    g.add(API::Reads{}, API::Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i); });

    auto report = g.run();
    EXPECT_TRUE(report.node_events.empty());
    EXPECT_EQ(report.clock.event_ns, 0u);
    EXPECT_EQ(report.clock.steady_ns, 0u);
}

// E.2 - three independent kernel nodes, run(5): fifteen records, invocations
// 0..4 per node, and submit <= start <= end for every one of them.
TEST(Integration_NodeEventRecords, RequestedGraphRecordsOnePerDispatch)
{
    constexpr std::size_t N = 64;

    auto rt = Runtime::create();
    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    auto c = rt.buffer<int>(N);
    int* pa = a.data();
    int* pb = b.data();
    int* pc = c.data();

    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add(API::Reads{}, API::Writes(a), N, [pa](std::size_t i) { pa[i] = static_cast<int>(i); });
    g.add(API::Reads{}, API::Writes(b), N, [pb](std::size_t i) { pb[i] = static_cast<int>(i); });
    g.add(API::Reads{}, API::Writes(c), N, [pc](std::size_t i) { pc[i] = static_cast<int>(i); });

    auto report = g.run(std::size_t{5});
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

// E.3 - lives in test_steady_state_allocation.cpp (sr_allocation_tests' own
// process): a replacement operator new here would only ever see this
// executable's allocations, never the shared library's (see that file's
// header comment, lines 24-49, for why it needs its own binary).

// E.4 - node_index follows registration order, not the compiled (reversed)
// order; cross-checked against the plan's own registration_index.
TEST(Integration_NodeEventRecords, NodeIndexIsRegistrationOrder)
{
    constexpr std::size_t N = 32;

    auto rt = Runtime::create();
    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    auto c = rt.buffer<int>(N);
    int* pa = a.data();
    int* pb = b.data();
    int* pc = c.data();

    // A strict dependency chain (B reads what A wrote, C reads what B wrote)
    // forces execution order A, B, C, giving an observation of identity that
    // is independent of trusting registration_index off the Node object: if
    // the plan's registration order and the run's actual dispatch order ever
    // disagreed, sorting by earliest submit would catch it even though both
    // still individually read as {0, 1, 2}.
    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add(API::Reads{}, API::Writes(a), N, [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name_last("A");
    g.add(API::Reads(a), API::Writes(b), N, [pa, pb](std::size_t i) { pb[i] = pa[i] + 1; }).name_last("B");
    g.add(API::Reads(b), API::Writes(c), N, [pb, pc](std::size_t i) { pc[i] = pb[i] + 1; }).name_last("C");

    auto report = g.run();

    const auto& plan = API::detail::NodeEventRecordsTestAccess::plan(g);
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

// E.6 - a native host node's converted record lands inside the run's event
// window, within the pairing's uncertainty_ns.
TEST(Integration_NodeEventRecords, HostNodeLandsOnEventClock)
{
    constexpr std::size_t N = 64;

    auto rt = Runtime::create();
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    // The host node reads what the kernel writes, chaining it strictly
    // behind the kernel's completion instead of leaving the two as
    // independent roots — so the host record's window sits inside, not just
    // near, the run's event-clock window.
    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add(API::Reads{}, API::Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i); }).name_last("kernel");
    g.add_host(API::Reads(buf), API::Writes{}, [p]()
    {
        volatile int sink = 0;
        for (std::size_t i = 0; i < N; ++i) sink += p[i];
        (void)sink;
    });

    auto report = g.run();
    ASSERT_FALSE(report.node_events.empty());

    const auto& plan = API::detail::NodeEventRecordsTestAccess::plan(g);
    const std::uint32_t idx_kernel = registration_index_of(plan, "kernel");

    // A host record's converted submit_ns and start_ns are identical (the
    // scheduler stamps the same raw steady time into both); a device
    // record's rarely are. That, not a stored flag, is what separates them
    // here (NodeEventRecord stays field-for-field identical to the engine's
    // NodeEvent, per the design's global constraints).
    const Core::NodeEventRecord* kernel_record = nullptr;
    const Core::NodeEventRecord* host_record = nullptr;
    for (const auto& rec : report.node_events)
    {
        if (rec.node_index == idx_kernel) kernel_record = &rec;
        else if (rec.submit_ns == rec.start_ns) host_record = &rec;
    }

    ASSERT_NE(kernel_record, nullptr) << "no kernel record found among node_events";
    ASSERT_NE(host_record, nullptr) << "no host record found among node_events";

    // The chain guarantees the host node dispatches no earlier than the
    // kernel's completion; the tolerance only covers the pairing's own
    // measurement uncertainty, not scheduling jitter between unrelated roots.
    EXPECT_GE(host_record->start_ns + report.clock.uncertainty_ns, kernel_record->end_ns)
        << "the host record must not land before the kernel it depends on completed";
    EXPECT_LE(host_record->start_ns, host_record->end_ns);
}
