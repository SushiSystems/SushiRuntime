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
 * @file test_offload_sink.cpp
 * @brief Tests the OffloadSink contract through its in-memory implementation.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/task/in_memory_offload_sink.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_sink.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

/** @brief Tests that a node given back reaches the sink. */
TEST(Small_OffloadSink, ResubmitReachesTheSink)
{
    InMemoryOffloadSink implementation;
    OffloadSink& sink = implementation;
    Node given_back;

    sink.resubmit(given_back);

    EXPECT_EQ(implementation.resubmitted(), std::vector<Node*>{&given_back});
}

/** @brief Tests that a node finished remotely reaches the sink. */
TEST(Small_OffloadSink, CompleteRemoteReachesTheSink)
{
    InMemoryOffloadSink implementation;
    OffloadSink& sink = implementation;
    Node finished;

    sink.complete_remote(finished);

    EXPECT_EQ(implementation.completed_remote(), std::vector<Node*>{&finished});
}

/** @brief Tests that a node that ends its run reaches the sink with its error. */
TEST(Small_OffloadSink, FailReachesTheSinkWithTheError)
{
    InMemoryOffloadSink implementation;
    OffloadSink& sink = implementation;
    Node failed;

    sink.fail(failed, Error{ErrorCode::InvalidState, "rule failed"});

    ASSERT_EQ(implementation.failures().size(), 1u);
    EXPECT_EQ(implementation.failures()[0].first, &failed);
    EXPECT_EQ(implementation.failures()[0].second.message, "rule failed");
}
