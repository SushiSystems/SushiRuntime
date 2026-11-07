/**************************************************************************/
/* test_refusing_offload_sink.cpp                                         */
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
 * @file test_refusing_offload_sink.cpp
 * @brief Tests RefusingOffloadSink against the OffloadSink contract: it refuses and counts.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <stdexcept>

#include <gtest/gtest.h>

#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_sink.hpp>

#include <SushiRuntime/task/refusing_offload_sink.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Graph::RefusingOffloadSink;

/** @brief Tests that resubmit() refuses the node it is given by throwing. */
TEST(Small_RefusingOffloadSink, ResubmitThrowsARuntimeError)
{
    RefusingOffloadSink sink;
    Internal::Graph::OffloadSink& port = sink;
    Internal::Graph::Node node;

    EXPECT_THROW(port.resubmit(node), std::runtime_error);
}

/** @brief Tests that every refusal is counted. */
TEST(Small_RefusingOffloadSink, CountsEachRefusal)
{
    RefusingOffloadSink sink;
    Internal::Graph::OffloadSink& port = sink;
    Internal::Graph::Node first;
    Internal::Graph::Node second;
    ASSERT_EQ(sink.refusals(), 0);

    EXPECT_THROW(port.resubmit(first), std::runtime_error);
    EXPECT_THROW(port.resubmit(second), std::runtime_error);

    EXPECT_EQ(sink.refusals(), 2);
}
