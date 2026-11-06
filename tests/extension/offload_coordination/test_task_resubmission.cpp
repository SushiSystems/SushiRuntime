/**************************************************************************/
/* test_task_resubmission.cpp                                             */
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
 * @file test_task_resubmission.cpp
 * @brief Tests reoffer_nodes(): a node the sink refuses is released with its first offer gone.
 * @see tests/extension/offload_coordination/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_sink.hpp>

#include <SushiRuntime/offload_coordination/held_nodes.hpp>
#include <SushiRuntime/offload_coordination/task_resubmission.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using std::chrono::milliseconds;

namespace
{
    /** @brief Returns a fixed point of the steady clock plus @p offset. */
    std::chrono::steady_clock::time_point at(milliseconds offset)
    {
        return std::chrono::steady_clock::time_point(std::chrono::hours(1)) + offset;
    }

    /** @brief Throws from resubmit(), so every node offered to it is refused. */
    class RefusingSink final : public Internal::Graph::OffloadSink
    {
        public:
            /** @brief Refuses the node by throwing std::runtime_error. */
            void resubmit(Internal::Graph::Node&) override
            {
                throw std::runtime_error("the sink refuses every node");
            }

            /** @brief Is never called by reoffer_nodes(). */
            void complete_remote(Internal::Graph::Node&) override
            {

            }

            /** @brief Is never called by reoffer_nodes(). */
            void fail(Internal::Graph::Node&, Error) override
            {

            }
    };
} // namespace

/** @brief Tests that a node the sink refuses is released and forgets its first offer. */
TEST(Small_TaskResubmission, Regression_ARefusedReofferForgetsTheFirstOffer)
{
    HeldNodes held;
    RefusingSink sink;
    const sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
    // The scheduling reference reoffer_nodes() releases; the test's own keeps the address.
    node->add_ref();
    ASSERT_EQ(node->ref_count(), 2);
    ASSERT_EQ(held.first_offer(*node, at(milliseconds(0))), at(milliseconds(0)));

    const std::vector<Internal::Graph::Node*> nodes{node.get()};
    EXPECT_EQ(reoffer_nodes(nodes, held, sink), 1u);

    EXPECT_EQ(node->ref_count(), 1) << "the refused node's scheduling reference was released";
    EXPECT_EQ(held.first_offer(*node, at(milliseconds(50))), at(milliseconds(50)))
        << "the released node kept the first offer of its earlier wait";
}
