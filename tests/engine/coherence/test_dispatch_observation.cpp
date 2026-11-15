/**************************************************************************/
/* test_dispatch_observation.cpp                                          */
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
 * @file test_dispatch_observation.cpp
 * @brief Tests that observe_dispatches() turns ledger records into measured work and drops what says nothing.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <SushiRuntime/coherence/dispatch_observation.hpp>

#include "fake_profile_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    NodeTable one_node_table(std::optional<NodeCost> cost)
    {
        NodeTableRow row;
        row.index = 0;
        row.cost = cost;
        return NodeTable({row});
    }

    NodeCost declared_cost()
    {
        NodeCost cost;
        cost.operations = 4000;
        cost.read_bytes = 500;
        cost.written_bytes = 500;
        cost.kind = OperationKinds::K_COMPUTE_BOUND;
        return cost;
    }

    NodeEventRecord record_on(std::uint32_t device_index, std::uint64_t start_ns, std::uint64_t end_ns)
    {
        NodeEventRecord record;
        record.node_index = 0;
        record.device_index = device_index;
        record.start_ns = start_ns;
        record.end_ns = end_ns;
        return record;
    }

    DeviceKeyTable two_devices_and_a_host()
    {
        DeviceKeyTable devices;
        devices.by_index = {host_key(1), host_key(2)};
        devices.host = host_key(3);
        return devices;
    }

    TEST(Small_DispatchObservation, ARecordGivesItsDeviceItsDeclaredCostAndItsSeconds)
    {
        const std::vector<DispatchObservation> observations =
            observe_dispatches({record_on(1, 1000, 251000)}, one_node_table(declared_cost()), two_devices_and_a_host());

        ASSERT_EQ(observations.size(), 1u);
        EXPECT_EQ(observations[0].device, host_key(2));
        EXPECT_EQ(observations[0].cost, declared_cost());
        EXPECT_DOUBLE_EQ(observations[0].seconds, 250000e-9);
    }

    TEST(Small_DispatchObservation, AHostRecordTakesTheHostKeyWhenTheTableHasOne)
    {
        DeviceKeyTable devices = two_devices_and_a_host();
        const NodeTable table = one_node_table(declared_cost());
        const std::vector<NodeEventRecord> records{record_on(K_HOST_DEVICE_INDEX, 0, 100)};

        const std::vector<DispatchObservation> with_host = observe_dispatches(records, table, devices);
        devices.host.reset();
        const std::vector<DispatchObservation> without_host = observe_dispatches(records, table, devices);

        ASSERT_EQ(with_host.size(), 1u);
        EXPECT_EQ(with_host[0].device, host_key(3));
        EXPECT_TRUE(without_host.empty());
    }

    TEST(Small_DispatchObservation, ARecordThatSaysNothingIsDropped)
    {
        const DeviceKeyTable devices = two_devices_and_a_host();
        NodeEventRecord unknown_node = record_on(0, 0, 100);
        unknown_node.node_index = 7;
        NodeCost no_operations = declared_cost();
        no_operations.operations = 0;

        EXPECT_TRUE(observe_dispatches({unknown_node}, one_node_table(declared_cost()), devices).empty());
        EXPECT_TRUE(observe_dispatches({record_on(0, 0, 100)}, one_node_table(std::nullopt), devices).empty());
        EXPECT_TRUE(observe_dispatches({record_on(0, 0, 100)}, one_node_table(no_operations), devices).empty());
        EXPECT_TRUE(observe_dispatches({record_on(5, 0, 100)}, one_node_table(declared_cost()), devices).empty());
        EXPECT_TRUE(observe_dispatches({record_on(0, 100, 100)}, one_node_table(declared_cost()), devices).empty());
        EXPECT_TRUE(observe_dispatches({record_on(0, 200, 100)}, one_node_table(declared_cost()), devices).empty());
    }

    TEST(Small_DispatchObservation, RecordsKeepTheirOrder)
    {
        const std::vector<DispatchObservation> observations = observe_dispatches(
            {record_on(0, 0, 1000), record_on(1, 0, 2000), record_on(0, 0, 3000)}, one_node_table(declared_cost()),
            two_devices_and_a_host());

        ASSERT_EQ(observations.size(), 3u);
        EXPECT_EQ(observations[0].device, host_key(1));
        EXPECT_EQ(observations[1].device, host_key(2));
        EXPECT_EQ(observations[2].device, host_key(1));
        EXPECT_LT(observations[0].seconds, observations[2].seconds);
    }
} // namespace
