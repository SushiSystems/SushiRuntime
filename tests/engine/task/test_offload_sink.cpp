/**************************************************************************/
/* test_offload_sink.cpp                                                  */
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
 * @file test_offload_sink.cpp
 * @brief Tests the OffloadSink contract through a recording implementation.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
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
            void fail(Node& node, Error error) override { failed = &node; failure = error.message; }

            OffloadSink* bound = nullptr;     /**< The sink bind() was given. */
            std::size_t prepared = 0;         /**< The nodes prepare() was given. */
            Node* offered = nullptr;          /**< The node try_offload() was given. */
            Node* completed = nullptr;        /**< The node completed locally. */
            Node* resubmitted = nullptr;      /**< The node given back. */
            Node* remote = nullptr;           /**< The node completed on a worker. */
            Node* failed = nullptr;           /**< The node that ended its run. */
            std::string failure;              /**< The message fail() was given. */
            bool stopped = false;             /**< Whether stop() was called. */
    };
}

/** @brief Tests that a node given back and a node finished remotely reach the sink. */
TEST(Small_OffloadSink, BothCallsReachTheSink)
{
    RecordingCoordinator recorder;
    OffloadSink& sink = recorder;
    Node given_back;
    Node finished;
    sink.resubmit(given_back);
    sink.complete_remote(finished);
    EXPECT_EQ(recorder.resubmitted, &given_back);
    EXPECT_EQ(recorder.remote, &finished);
}

/** @brief Tests that a node that ends its run reaches the sink with its error. */
TEST(Small_OffloadSink, FailReachesTheSinkWithTheError)
{
    RecordingCoordinator recorder;
    OffloadSink& sink = recorder;
    Node failed;
    sink.fail(failed, Error{ErrorCode::InvalidState, "rule failed"});
    EXPECT_EQ(recorder.failed, &failed);
    EXPECT_EQ(recorder.failure, "rule failed");
}
