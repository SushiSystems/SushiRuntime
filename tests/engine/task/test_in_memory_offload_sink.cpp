/**************************************************************************/
/* test_in_memory_offload_sink.cpp                                        */
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
 * @file test_in_memory_offload_sink.cpp
 * @brief Tests the InMemoryOffloadSink adapter against the OffloadSink contract.
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

/** @brief Tests that every node given back is recorded in call order. */
TEST(Small_InMemoryOffloadSink, RecordsEachResubmittedNodeInCallOrder)
{
    InMemoryOffloadSink sink;
    OffloadSink& port = sink;
    Node first;
    Node second;
    port.resubmit(first);
    port.resubmit(second);
    EXPECT_EQ(sink.resubmitted(), (std::vector<Node*>{&first, &second}));
}

/** @brief Tests that every node a worker finished is recorded in call order. */
TEST(Small_InMemoryOffloadSink, RecordsEachRemotelyCompletedNodeInCallOrder)
{
    InMemoryOffloadSink sink;
    OffloadSink& port = sink;
    Node first;
    Node second;
    port.complete_remote(first);
    port.complete_remote(second);
    EXPECT_EQ(sink.completed_remote(), (std::vector<Node*>{&first, &second}));
}

/** @brief Tests that a failed node is recorded with the error it ended with. */
TEST(Small_InMemoryOffloadSink, RecordsEachFailedNodeWithItsError)
{
    InMemoryOffloadSink sink;
    OffloadSink& port = sink;
    Node failed;
    port.fail(failed, Error{ErrorCode::InvalidState, "rule failed"});
    ASSERT_EQ(sink.failures().size(), 1u);
    EXPECT_EQ(sink.failures()[0].first, &failed);
    EXPECT_EQ(sink.failures()[0].second.code, ErrorCode::InvalidState);
    EXPECT_EQ(sink.failures()[0].second.message, "rule failed");
}
