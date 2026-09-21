/**************************************************************************/
/* test_node_dispatch_width.cpp                                          */
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

// A node's width is stamped after the node is added, the same way its name is,
// because add_node returns void and the emitters build the node before they
// know which of several internal widths it took.

#include <cstddef>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/run_report.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;

namespace
{
    /** @brief A no-op device node body; these tests check bookkeeping, not results. */
    TaskWork noop_work()
    {
        return [](sycl::handler&) {};
    }
}

TEST(Unit_NodeDispatchWidth, UnstampedNodeReportsUnknown)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    graph.add_node(noop_work());

    EXPECT_EQ(graph.last_node_width(), std::size_t{Core::WIDTH_UNKNOWN});
}

TEST(Unit_NodeDispatchWidth, WidthLastNodeStampsTheNodeNameLastNodeNames)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    graph.add_node(noop_work());
    graph.width_last_node(4096);
    graph.name_last_node("stamped");

    EXPECT_EQ(graph.last_node_width(), std::size_t{4096});
    ASSERT_NE(graph.last_node_added(), nullptr);
    EXPECT_STREQ(graph.last_node_added()->metadata.name, "stamped");
}

TEST(Unit_NodeDispatchWidth, WidthLastNodeOnAnEmptyGraphIsHarmless)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    graph.width_last_node(4096);

    EXPECT_EQ(graph.last_node_width(), std::size_t{Core::WIDTH_UNKNOWN});
    EXPECT_EQ(graph.last_node_added(), nullptr);
}
