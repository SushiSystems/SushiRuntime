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
 * @file test_hw_discovery.cpp
 * @brief Tests what HardwareDiscovery returns for the filters a caller can state.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <set>
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

/** @brief Tests that discovery finds a device, numbers the devices from zero in order and names each. */
TEST(Medium_HardwareDiscovery, LogicalIDsAreSequentialAndEveryDeviceIsNamed)
{
    HardwareDiscovery discovery;
    DeviceFilter config;
    config.include_cpu = true;
    config.include_gpu = true;
    config.include_accelerator = true;

    const std::vector<HardwareNode> nodes = discovery.probe_all_devices(config).value();
    ASSERT_FALSE(nodes.empty()) << "hardware discovery found no device";
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        EXPECT_EQ(nodes[i].logical_id, i) << "logical IDs are not sequential";
        EXPECT_FALSE(nodes[i].name.empty()) << "device " << i << " has no name";
    }
}

TEST(Small_HwDiscovery, LogsAPciAddressThatDoesNotParse)
{
    SushiRuntime::TestRunner::LogCapture capture(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT,
                                                 SushiLog::LogLevel::Debug);

    const auto parsed = parse_pci_address("0000:01:00.0", "gpu0");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->bus, 1u);
    EXPECT_TRUE(capture.records().empty());

    EXPECT_FALSE(parse_pci_address("no address here", "gpu0").has_value());
    ASSERT_EQ(capture.count_at(SushiLog::LogLevel::Debug), 1u);
    EXPECT_EQ(capture.count_in("topology"), 1u);
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Debug, "no address here"));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Debug, "gpu0"));
}
