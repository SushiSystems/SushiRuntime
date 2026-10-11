/**************************************************************************/
/* test_region_commit.cpp                                                 */
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
 * @file test_region_commit.cpp
 * @brief Tests what a DynamicGraph keeps when the commit of its regions throws.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <stdexcept>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "dynamic_graph_plan_access.hpp"
#include "refusing_fill.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Number of elements of every buffer in this file. */
    constexpr std::size_t K_ELEMENTS = 8;

    /** @brief Message of the exception the refused copy of a task throws. */
    constexpr const char* K_BUILD_FAILURE = "the task refused to build";

    /** @brief Returns the plan of a graph that requested profiling. */
    const Internal::Graph::CompiledGraph& plan_of(const DynamicGraph& world)
    {
        return Detail::DynamicGraphNodeEventsTestAccess::plan(world);
    }

    /** @brief Copies K_ELEMENTS elements from one buffer to another. */
    void copy_elements(const int* from, int* to)
    {
        for (std::size_t i = 0; i < K_ELEMENTS; ++i)
            to[i] = from[i];
    }

    /** @brief Expects the cross-region edge from the node named "a" to the node named "c". */
    void expect_producer_orders_consumer(const DynamicGraph& world)
    {
        const Internal::Graph::Node* a = Testing::find_plan_node(plan_of(world), "a");
        const Internal::Graph::Node* c = Testing::find_plan_node(plan_of(world), "c");
        ASSERT_NE(a, nullptr);
        ASSERT_NE(c, nullptr);
        EXPECT_TRUE(Testing::has_boundary_edge(*a, *c));
        EXPECT_EQ(Testing::boundary_in_degree(*c), 1);
    }
} // namespace

/** @brief Tests that a commit whose region build throws keeps the edges of the other regions. */
TEST(Integration_RegionCommit, AThrowingRegionBuildKeepsTheEdgesOfTheOtherRegions)
{
    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto shared = rt.buffer<int>(K_ELEMENTS).value();
    auto out_b = rt.buffer<int>(K_ELEMENTS).value();
    auto out_c = rt.buffer<int>(K_ELEMENTS).value();
    auto own_d = rt.buffer<int>(K_ELEMENTS).value();
    for (std::size_t i = 0; i < K_ELEMENTS; ++i)
    {
        shared[i] = 0;
        out_b[i] = 0;
        out_c[i] = 0;
        own_d[i] = 0;
    }
    int* ps = shared.data();
    int* pb = out_b.data();
    int* pc = out_c.data();
    int* pd = own_d.data();

    auto world = rt.dynamic_graph();
    world.set_profiling(true);
    world.region(0).add_host(Reads{}, Writes(shared),
                             [ps]() { for (std::size_t i = 0; i < K_ELEMENTS; ++i) ps[i] += 1; })
                   .name("a");
    world.region(1).add_host(Reads(shared), Writes(out_b),
                             [ps, pb]() { copy_elements(ps, pb); })
                   .name("b");
    world.region(2).add_host(Reads(shared), Writes(out_c),
                             [ps, pc]() { copy_elements(ps, pc); })
                   .name("c");
    world.run().value();
    expect_producer_orders_consumer(world);
    const std::size_t composed = world.compose_count();

    const auto refuses = std::make_shared<bool>(true);
    world.drop(1);
    world.region(3).add_host(Reads{}, Writes(own_d),
                             Testing::RefusingFill(refuses, K_BUILD_FAILURE, pd, K_ELEMENTS, 5));

    const auto failed = world.run();
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, ErrorCode::CompileFailed);
    EXPECT_EQ(world.compose_count(), composed) << "a commit that threw composed nothing";
    expect_producer_orders_consumer(world);

    *refuses = false;
    const auto retried = world.run();
    ASSERT_TRUE(retried.has_value());
    EXPECT_EQ(world.compose_count(), composed + 1);
    expect_producer_orders_consumer(world);

    for (std::size_t i = 0; i < K_ELEMENTS; ++i)
    {
        ASSERT_EQ(shared[i], 2) << "at i=" << i;
        ASSERT_EQ(out_c[i], 2) << "at i=" << i;
        ASSERT_EQ(out_b[i], 1) << "the dropped region ran once, at i=" << i;
        ASSERT_EQ(own_d[i], 5) << "at i=" << i;
    }
}
