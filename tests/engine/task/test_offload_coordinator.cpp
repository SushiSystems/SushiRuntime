/**************************************************************************/
/* test_offload_coordinator.cpp                                           */
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
 * @file test_offload_coordinator.cpp
 * @brief Tests the OffloadCoordinator contract through a recording implementation.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_sink.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /** @brief Records what the scheduler asks of an offload coordinator and its sink. */
    class RecordingCoordinator final : public OffloadCoordinator, public OffloadSink
    {
        public:
            void bind(OffloadSink& sink) override { bound = &sink; }
            void prepare(span<const sushi_ptr<Node>> nodes) override { prepared = nodes.size(); }
            bool try_offload(Node& node) override { offered = &node; return false; }
            void on_local_completion(Node& node) noexcept override { completed = &node; }
            void stop() noexcept override { stopped = true; }
            void resubmit(Node& node) override { resubmitted = &node; }
            void complete_remote(Node& node) override { remote = &node; }

            OffloadSink* bound = nullptr;     /**< The sink bind() was given. */
            std::size_t prepared = 0;         /**< The nodes prepare() was given. */
            Node* offered = nullptr;          /**< The node try_offload() was given. */
            Node* completed = nullptr;        /**< The node completed locally. */
            Node* resubmitted = nullptr;      /**< The node given back. */
            Node* remote = nullptr;           /**< The node completed on a worker. */
            bool stopped = false;             /**< Whether stop() was called. */
    };
}

/** @brief Tests that each member of the seam reaches the implementation with its argument. */
TEST(Small_OffloadCoordinator, EveryMemberReachesTheImplementation)
{
    RecordingCoordinator recorder;
    OffloadCoordinator& coordinator = recorder;
    std::vector<sushi_ptr<Node>> nodes{make_sushi<Node>(), make_sushi<Node>()};

    coordinator.bind(recorder);
    coordinator.prepare(span<const sushi_ptr<Node>>(nodes.data(), nodes.size()));
    EXPECT_FALSE(coordinator.try_offload(*nodes[0]));
    coordinator.on_local_completion(*nodes[1]);
    coordinator.stop();

    EXPECT_EQ(recorder.bound, &recorder);
    EXPECT_EQ(recorder.prepared, 2u);
    EXPECT_EQ(recorder.offered, nodes[0].get());
    EXPECT_EQ(recorder.completed, nodes[1].get());
    EXPECT_TRUE(recorder.stopped);
}
