/**************************************************************************/
/* test_profile_store.cpp                                                 */
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
 * @file test_profile_store.cpp
 * @brief Tests that ProfileStore folds what any session measured into the one model every session reads.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <memory>
#include <thread>
#include <vector>

#include <SushiRuntime/partition_session/profile_store.hpp>

#include "support/partition_problem_builder.hpp"
#include "rebalancing_parts.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_ProfileStore, AStoreOverNoModelHoldsNone)
    {
        const ProfileStore store(nullptr);

        EXPECT_EQ(store.model(), nullptr);
    }

    TEST(Small_ProfileStore, AFoldReplacesTheModelEveryReaderSees)
    {
        ProfileStore store(speed_model({1.0, 1.0}));
        const std::shared_ptr<const CostModel> before = store.model();

        const std::shared_ptr<const CostModel> folded = store.fold(step_dispatches({{host_key(2), 2.5}}));

        EXPECT_NE(folded, before);
        EXPECT_EQ(store.model(), folded);
        // 1000 operations in 2.5 s with no bytes is 400 operations a second.
        EXPECT_EQ(store.model()->profiles().find(host_key(2))->base_throughput, 400.0);
        EXPECT_EQ(before->profiles().find(host_key(2))->base_throughput,
                  speed_model({1.0, 1.0})->profiles().find(host_key(2))->base_throughput);
    }

    TEST(Small_ProfileStore, AFoldOfNothingKeepsTheModel)
    {
        ProfileStore store(speed_model({1.0, 1.0}));
        const std::shared_ptr<const CostModel> before = store.model();

        EXPECT_EQ(store.fold({}), before);
    }

    TEST(Small_ProfileStore, TwoSessionsFoldingAtOnceBothReachTheModel)
    {
        ProfileStore store(speed_model({1.0, 1.0}));

        std::thread first([&store]() { static_cast<void>(store.fold(step_dispatches({{host_key(1), 2.5}}))); });
        std::thread second([&store]() { static_cast<void>(store.fold(step_dispatches({{host_key(2), 2.5}}))); });
        first.join();
        second.join();

        EXPECT_EQ(store.model()->profiles().find(host_key(1))->base_throughput, 400.0);
        EXPECT_EQ(store.model()->profiles().find(host_key(2))->base_throughput, 400.0);
    }
} // namespace
