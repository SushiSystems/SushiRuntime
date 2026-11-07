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
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/machine/device_kind.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/hwloc_numa_reader.hpp>
#include <SushiRuntime/topology/in_memory_device_enumerator.hpp>
#include <SushiRuntime/topology/in_memory_numa_reader.hpp>
#include <SushiRuntime/topology/sycl_device_enumerator.hpp>

using namespace SushiRuntime::Internal::Topology;
using SushiRuntime::DeviceFilter;
using SushiRuntime::DeviceKind;

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
        SYCLDeviceEnumerator enumerator;
        HwlocNUMAReader reader;
        HardwareDiscovery discovery(enumerator, reader);
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

// Holds on any machine; it can fail only where two backends expose one GPU.
TEST(Medium_HardwareDiscovery, NoTwoNodesShareAPCIAddress)
{
    using PCIKey = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>;

    SYCLDeviceEnumerator enumerator;
    HwlocNUMAReader reader;
    HardwareDiscovery discovery(enumerator, reader);
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

namespace
{
    /** @brief Returns one enumerated device of a class, named, with its fp64 flag and PCI text. */
    DeviceDescriptor listed(const char* name, DeviceKind kind, std::string pci_text = {}, bool fp64 = true)
    {
        DeviceDescriptor device;
        device.name = name;
        device.kind = kind;
        device.fp64 = fp64;
        device.global_mem_size = 1024;
        device.max_compute_units = 4;
        device.pci_text = std::move(pci_text);
        return device;
    }

    /** @brief Probes an in-memory list through @p reader. */
    std::vector<HardwareNode> probe(std::vector<DeviceDescriptor> devices, const DeviceFilter& config = {},
                                    InMemoryNUMAReader reader = InMemoryNUMAReader(0))
    {
        InMemoryDeviceEnumerator enumerator(std::move(devices));
        HardwareDiscovery discovery(enumerator, reader);
        return discovery.probe_all_devices(config).value();
    }

    /** @brief Returns the names of the nodes, in order. */
    std::vector<std::string> names_of(const std::vector<HardwareNode>& nodes)
    {
        std::vector<std::string> names;
        for (const HardwareNode& node : nodes)
            names.push_back(node.name);
        return names;
    }
} // namespace

/** @brief Tests that a filter that admits no class yields no device and no error. */
TEST(Small_HardwareDiscovery, AFilterThatAdmitsNoClassYieldsNoDevice)
{
    EXPECT_TRUE(probe({listed("cpu", DeviceKind::CPU), listed("gpu", DeviceKind::GPU)},
                      filter_admitting_nothing()).empty());
}

/** @brief Tests that a kept node carries the enumerator's id and class of its device. */
TEST(Small_HardwareDiscovery, AKeptNodeCarriesTheIdAndClassItWasListedWith)
{
    DeviceDescriptor cpu = listed("cpu", DeviceKind::CPU);
    cpu.id = 0;
    DeviceDescriptor accelerator = listed("fpga", DeviceKind::Accelerator);
    accelerator.id = 7;

    const std::vector<HardwareNode> nodes = probe({cpu, accelerator});

    ASSERT_EQ(nodes.size(), 2U);
    EXPECT_EQ(nodes[1].device_id, 7U);
    EXPECT_EQ(nodes[1].kind, DeviceKind::Accelerator);
}

/** @brief Tests that the GPU-only filter keeps the GPUs of a mixed list and nothing else. */
TEST(Small_HardwareDiscovery, TheGPUOnlyFilterKeepsOnlyTheGPUs)
{
    DeviceFilter config;
    config.include_cpu = false;
    config.include_accelerator = false;

    const std::vector<HardwareNode> nodes =
        probe({listed("cpu", DeviceKind::CPU), listed("gpu a", DeviceKind::GPU),
               listed("fpga", DeviceKind::Accelerator), listed("gpu b", DeviceKind::GPU)},
              config);

    EXPECT_EQ(names_of(nodes), (std::vector<std::string>{"gpu a", "gpu b"}));
    for (const HardwareNode& node : nodes)
        EXPECT_TRUE(node.is_gpu);
}

/** @brief Tests that the kept devices are numbered from zero in order, a dropped one leaving no gap. */
TEST(Small_HardwareDiscovery, LogicalIDsAreSequentialOverTheKeptDevices)
{
    const std::vector<HardwareNode> nodes =
        probe({listed("cpu a", DeviceKind::CPU), listed("cpu b", DeviceKind::CPU),
               listed("gpu", DeviceKind::GPU, "0000:01:00.0")});

    EXPECT_EQ(names_of(nodes), (std::vector<std::string>{"cpu a", "gpu"}));
    EXPECT_EQ(first_unsequential_id(nodes), "");
}

/** @brief Tests that a CPU takes the reader's node, and a reader that names none leaves it at -1. */
TEST(Small_HardwareDiscovery, ACPUTakesTheReadersNode)
{
    EXPECT_EQ(probe({listed("cpu", DeviceKind::CPU)}, {}, InMemoryNUMAReader(3))[0].numa_node_id, 3);
    EXPECT_EQ(probe({listed("cpu", DeviceKind::CPU)}, {}, InMemoryNUMAReader())[0].numa_node_id, -1);
}

/** @brief Tests that a second device at one PCI address is dropped. */
TEST(Small_HardwareDiscovery, ASecondDeviceAtOnePCIAddressIsDropped)
{
    const std::vector<HardwareNode> nodes =
        probe({listed("gpu cuda", DeviceKind::GPU, "0000:01:00.0"),
               listed("gpu opencl", DeviceKind::GPU, "0000:01:00.0"),
               listed("gpu other", DeviceKind::GPU, "0000:02:00.0")});

    EXPECT_EQ(names_of(nodes), (std::vector<std::string>{"gpu cuda", "gpu other"}));
}

/** @brief Tests that a GPU whose driver names no address takes the one the reader lists for its name. */
TEST(Small_HardwareDiscovery, AGPUWithNoDriverAddressTakesTheReadersAddress)
{
    const std::vector<HardwareNode> nodes =
        probe({listed("NVIDIA RTX 4090", DeviceKind::GPU)}, {},
              InMemoryNUMAReader(0, {ListedGPU{"RTX", "0000:0a:00.0"}}));

    ASSERT_TRUE(nodes[0].pci_address.has_value());
    EXPECT_EQ(nodes[0].pci_address->bus, 0x0au);
}

/** @brief Tests that a device without fp64 is dropped when the filter requires it. */
TEST(Small_HardwareDiscovery, ADeviceWithoutFP64IsDroppedWhenRequired)
{
    DeviceFilter config;
    config.require_fp64 = true;

    const std::vector<HardwareNode> nodes =
        probe({listed("gpu single", DeviceKind::GPU, "", false), listed("gpu double", DeviceKind::GPU)}, config);

    EXPECT_EQ(names_of(nodes), (std::vector<std::string>{"gpu double"}));
}

/** @brief Tests that an enumeration that fails is an Unsupported error naming the runtime's text. */
TEST(Small_HardwareDiscovery, AFailedEnumerationIsUnsupported)
{
    InMemoryDeviceEnumerator enumerator = InMemoryDeviceEnumerator::failing("no platform");
    InMemoryNUMAReader reader(0);
    HardwareDiscovery discovery(enumerator, reader);

    const auto probed = discovery.probe_all_devices();

    ASSERT_FALSE(probed.has_value());
    EXPECT_EQ(probed.error().code, SushiRuntime::ErrorCode::Unsupported);
    EXPECT_NE(probed.error().message.find("no platform"), std::string::npos);
}
