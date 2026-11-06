/**************************************************************************/
/* test_hw_discovery.cpp                                                  */
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
 * @file test_hw_discovery.cpp
 * @brief Tests what HardwareDiscovery returns for the filters a caller can state.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/test_runner/log_capture.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>

using namespace SushiRuntime::Internal::Topology;
using SushiRuntime::DeviceFilter;

namespace
{
    /** @brief Returns the filter that admits no device class. */
    DeviceFilter filter_admitting_nothing()
    {
        DeviceFilter config;
        config.include_cpu = false;
        config.include_gpu = false;
        config.include_accelerator = false;
        return config;
    }

    /** @brief Probes every device class and returns the nodes found. */
    std::vector<HardwareNode> probe_every_class()
    {
        HardwareDiscovery discovery;
        DeviceFilter config;
        config.include_cpu = true;
        config.include_gpu = true;
        config.include_accelerator = true;
        return discovery.probe_all_devices(config).value();
    }

    /** @brief Describes the first node whose logical ID is not its position, or returns an empty text. */
    std::string first_unsequential_id(const std::vector<HardwareNode>& nodes)
    {
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            if (nodes[i].logical_id != i)
                return "device " + std::to_string(i) + " has the logical ID " + std::to_string(nodes[i].logical_id);
        }
        return {};
    }

    /** @brief Describes the first node with no name, or returns an empty text. */
    std::string first_unnamed_device(const std::vector<HardwareNode>& nodes)
    {
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            if (nodes[i].name.empty())
                return "device " + std::to_string(i) + " has no name";
        }
        return {};
    }
} // namespace

// An empty result is a value at this tier, not a failure; see README.md.
TEST(Medium_HardwareDiscovery, AFilterThatAdmitsNoClassYieldsNoDeviceAndDoesNotThrow)
{
    HardwareDiscovery discovery;
    const auto probed = discovery.probe_all_devices(filter_admitting_nothing());
    ASSERT_TRUE(probed.has_value()) << probed.error().message;
    EXPECT_TRUE(probed->empty());

    HwlocTopologyProvider provider;
    const auto enumerated = provider.enumerate(filter_admitting_nothing());
    ASSERT_TRUE(enumerated.has_value()) << enumerated.error().message;
    EXPECT_TRUE(enumerated->empty());
}

// Holds on any machine; it can fail only where two backends expose one GPU.
TEST(Medium_HardwareDiscovery, NoTwoNodesShareAPCIAddress)
{
    using PCIKey = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>;

    HardwareDiscovery discovery;
    std::set<PCIKey> seen;
    const auto probed = discovery.probe_all_devices();
    ASSERT_TRUE(probed.has_value()) << probed.error().message;
    for (const HardwareNode& node : *probed)
    {
        if (!node.pci_address.has_value())
            continue;
        const PCIDomainAddress& address = *node.pci_address;
        EXPECT_TRUE(seen.insert(PCIKey{address.domain, address.bus, address.device,
                                       address.function}).second)
            << "device [" << node.name << "] repeats a PCI address already returned";
    }
}

TEST(Medium_HardwareDiscovery, EveryNodeTheGPUOnlyFilterAdmitsIsAGPU)
{
    DeviceFilter config;
    config.include_cpu = false;
    config.include_accelerator = false;

    HardwareDiscovery discovery;
    for (const HardwareNode& node : discovery.probe_all_devices(config).value())
        EXPECT_TRUE(node.sycl_dev.is_gpu()) << "device [" << node.name << "] is not a GPU";
}

/** @brief Tests that discovery finds a device, numbers the devices from zero in order. */
TEST(Medium_HardwareDiscovery, LogicalIDsAreSequential)
{
    const std::vector<HardwareNode> nodes = probe_every_class();
    ASSERT_FALSE(nodes.empty()) << "hardware discovery found no device";

    EXPECT_EQ(first_unsequential_id(nodes), "");
}

/** @brief Tests that every device discovery returns is named. */
TEST(Medium_HardwareDiscovery, EveryDeviceIsNamed)
{
    const std::vector<HardwareNode> nodes = probe_every_class();
    ASSERT_FALSE(nodes.empty()) << "hardware discovery found no device";

    EXPECT_EQ(first_unnamed_device(nodes), "");
}

/** @brief Tests that a PCI address written as domain:bus:device.function parses to its four fields. */
TEST(Small_HwDiscovery, AValidPciAddressParsesToItsFields)
{
    const auto parsed = parse_pci_address("0001:02:03.4", "gpu0");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->domain, 1u);
    EXPECT_EQ(parsed->bus, 2u);
    EXPECT_EQ(parsed->device, 3u);
    EXPECT_EQ(parsed->function, 4u);
}

/** @brief Tests that a PCI address that parses writes no log record. */
TEST(Small_HwDiscovery, AValidPciAddressLogsNothing)
{
    SushiRuntime::TestRunner::LogCapture capture(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT,
                                                 SushiLog::LogLevel::Debug);

    static_cast<void>(parse_pci_address("0000:01:00.0", "gpu0"));

    EXPECT_TRUE(capture.records().empty());
}

/** @brief Tests that text that holds no PCI address returns nothing. */
TEST(Small_HwDiscovery, AnAddressThatDoesNotParseReturnsNothing)
{
    EXPECT_FALSE(parse_pci_address("no address here", "gpu0").has_value());
}

/** @brief Tests that text that holds no PCI address logs one DEBUG line naming the text and the device. */
TEST(Small_HwDiscovery, AnAddressThatDoesNotParseLogsOneDebugLineNamingTheTextAndTheDevice)
{
    SushiRuntime::TestRunner::LogCapture capture(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT,
                                                 SushiLog::LogLevel::Debug);

    static_cast<void>(parse_pci_address("no address here", "gpu0"));

    ASSERT_EQ(capture.count_at(SushiLog::LogLevel::Debug), 1u);
    EXPECT_EQ(capture.count_in("topology"), 1u);
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Debug, "no address here"));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Debug, "gpu0"));
}
