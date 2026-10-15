/**************************************************************************/
/* test_boundary_access.cpp                                               */
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
 * @file test_boundary_access.cpp
 * @brief Tests compute_boundary_access(): the entry and exit pins of a region's nodes.
 * @see tests/engine/region_boundary/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/node.hpp>

#include <SushiRuntime/region_boundary/boundary_access.hpp>

using namespace SushiRuntime;

namespace BoundaryAccessTests
{
    namespace
    {
        using Interval = BufferRegion;
        using Intervals = std::vector<BufferRegion>;

        /** @brief Number of bytes of the allocation the pin tests slice. */
        constexpr std::size_t K_BYTES = 20;

        /** @brief Returns the pin of a node in a pin list, or nullptr when the node has none. */
        const Detail::Pin* pin_of(const std::vector<Detail::Pin>& pins,
                                       const Internal::Graph::Node& node)
        {
            for (const auto& pin : pins)
                if (pin.node == &node)
                    return &pin;
            return nullptr;
        }

        /** @brief Returns whether one interval of a list fully contains wanted. */
        bool holds(const Intervals& intervals, const Interval& wanted)
        {
            return Detail::covered_by_any(wanted, intervals);
        }
    } // namespace

    /** @brief Tests that a read reaching past its predecessor's write keeps an entry pin. */
    TEST(Small_BoundaryAccess, AReadPastItsPredecessorsWriteKeepsAnEntryPin)
    {
        unsigned char bytes[K_BYTES] = {};
        void* base = bytes;

        Internal::Graph::TaskGraph graph;
        Internal::Graph::Node& p = graph.add_native({}, {Interval(base, 0, 10)}, [] {});
        Internal::Graph::Node& node = graph.add_native({Interval(base, 5, 15)}, {}, [] {});
        const auto nodes = graph.take_nodes();

        const auto access = Detail::compute_boundary_access(nodes);
        ASSERT_EQ(access.count(base), 1u);
        const auto& entry = access.at(base).entry;

        ASSERT_NE(pin_of(entry, p), nullptr);
        const Detail::Pin* q_pin = pin_of(entry, node);
        ASSERT_NE(q_pin, nullptr) << "bytes [15,20) of q are ordered behind no earlier region";
        EXPECT_TRUE(holds(q_pin->reads, Interval(base, 15, 5)));
        EXPECT_TRUE(q_pin->writes.empty());
    }

    /** @brief Tests that a read inside its predecessor's write has no entry pin. */
    TEST(Small_BoundaryAccess, AReadInsideItsPredecessorsWriteHasNoEntryPin)
    {
        unsigned char bytes[K_BYTES] = {};
        void* base = bytes;

        Internal::Graph::TaskGraph graph;
        Internal::Graph::Node& p = graph.add_native({}, {Interval(base, 0, 20)}, [] {});
        Internal::Graph::Node& node = graph.add_native({Interval(base, 5, 15)}, {}, [] {});
        const auto nodes = graph.take_nodes();

        const auto access = Detail::compute_boundary_access(nodes);
        const auto& entry = access.at(base).entry;

        EXPECT_NE(pin_of(entry, p), nullptr);
        EXPECT_EQ(pin_of(entry, node), nullptr) << "p carries every byte q reads";
        EXPECT_EQ(entry.size(), 1u);
    }

    /** @brief Tests that a write behind a predecessor that only reads keeps its write pin. */
    TEST(Small_BoundaryAccess, AWriteBehindAReadingPredecessorKeepsItsWritePin)
    {
        unsigned char bytes[K_BYTES] = {};
        void* base = bytes;

        Internal::Graph::TaskGraph graph;
        Internal::Graph::Node& p = graph.add_native({Interval(base, 0, 10)}, {}, [] {});
        Internal::Graph::Node& node = graph.add_native({}, {Interval(base, 0, 10)}, [] {});
        const auto nodes = graph.take_nodes();

        const auto access = Detail::compute_boundary_access(nodes);
        const auto& entry = access.at(base).entry;

        ASSERT_NE(pin_of(entry, p), nullptr);
        const Detail::Pin* q_pin = pin_of(entry, node);
        ASSERT_NE(q_pin, nullptr) << "a reader does not follow the readers of earlier regions";
        EXPECT_TRUE(holds(q_pin->writes, Interval(base, 0, 10)));
    }

    /** @brief Tests that only a successor's write that contains an access shadows it on exit. */
    TEST(Small_BoundaryAccess, OnlyAContainingSuccessorWriteShadowsAnExit)
    {
        unsigned char bytes[K_BYTES] = {};
        void* base = bytes;

        Internal::Graph::TaskGraph contained;
        Internal::Graph::Node& inner = contained.add_native({}, {Interval(base, 5, 5)}, [] {});
        Internal::Graph::Node& outer = contained.add_native({}, {Interval(base, 0, 20)}, [] {});
        const auto contained_nodes = contained.take_nodes();
        const auto shadowed = Detail::compute_boundary_access(contained_nodes);
        EXPECT_EQ(pin_of(shadowed.at(base).exit, inner), nullptr);
        EXPECT_NE(pin_of(shadowed.at(base).exit, outer), nullptr);

        Internal::Graph::TaskGraph partial;
        Internal::Graph::Node& wide = partial.add_native({}, {Interval(base, 0, 20)}, [] {});
        Internal::Graph::Node& narrow = partial.add_native({}, {Interval(base, 5, 5)}, [] {});
        const auto partial_nodes = partial.take_nodes();
        const auto kept = Detail::compute_boundary_access(partial_nodes);
        EXPECT_NE(pin_of(kept.at(base).exit, wide), nullptr) << "bytes outside [5,10) stay live";
        EXPECT_NE(pin_of(kept.at(base).exit, narrow), nullptr);
    }
} // namespace BoundaryAccessTests
