/**************************************************************************/
/* hw_discovery.cpp                                                       */
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
 * @file hw_discovery.cpp
 * @brief Filters the enumerated devices and resolves each one's NUMA node or PCI address.
 * @author Mustafa Garip
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <cstdio>
#include <optional>
#include <regex>
#include <set>
#include <utility>
#include <string>
#include <tuple>
#include <unordered_set>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/topology/topology_log_category.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/machine/device_filter.hpp>
#include <SushiRuntime/machine/device_kind.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        namespace
        {
            /** @brief Spells a filter as "cpu=yes, gpu=no, accelerator=no, require_fp64=no". */
            std::string describe(const DeviceFilter& config)
            {
                std::string text = "cpu=";
                text += config.include_cpu ? "yes" : "no";
                text += ", gpu=";
                text += config.include_gpu ? "yes" : "no";
                text += ", accelerator=";
                text += config.include_accelerator ? "yes" : "no";
                text += ", require_fp64=";
                text += config.require_fp64 ? "yes" : "no";
                return text;
            }

            /** @brief Names one PCI function by its four address fields, in bus order. */
            using PCIKey = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>;

            /** @brief Returns the key two devices share when they sit at one PCI address. */
            PCIKey pci_key(const PCIDomainAddress& address) noexcept
            {
                return PCIKey{address.domain, address.bus, address.device, address.function};
            }

            /** @brief Spells a PCI address as "0000:01:00.0". */
            std::string describe(const PCIDomainAddress& address)
            {
                char text[40];
                std::snprintf(text, sizeof(text), "%04x:%02x:%02x.%x",
                              static_cast<unsigned>(address.domain),
                              static_cast<unsigned>(address.bus),
                              static_cast<unsigned>(address.device),
                              static_cast<unsigned>(address.function));
                return text;
            }

            /** @brief Builds the error of a SYCL enumeration that threw. */
            Error enumeration_error(const DeviceFilter& config, const char* sycl_text)
            {
                return Error{
                    ErrorCode::Unsupported,
                    std::string("HardwareDiscovery::probe_all_devices: SYCL device enumeration "
                                "failed: ") + sycl_text + ". Active filter: " + describe(config) +
                        ". Check that the device drivers are installed and that one SYCL runtime "
                        "is on the loader path; a second one beside it breaks enumeration."};
            }
        } // namespace

        std::optional<PCIDomainAddress> parse_pci_address(const std::string& text,
                                                          const std::string& device_name)
        {
            const std::regex pci_regex("([0-9a-fA-F]+):([0-9a-fA-F]+):([0-9a-fA-F]+)\\.([0-9a-fA-F]+)");
            std::smatch match;
            if (!std::regex_search(text, match, pci_regex) || match.size() != 5)
            {
                SR_LOG_DEBUG(K_TOPOLOGY_LOG_CATEGORY,
                             "PCI address text '{}' of device {} does not parse; the device stays "
                             "unmapped by PCI address.",
                             text, device_name);
                return std::nullopt;
            }

            PCIDomainAddress address;
            address.domain = static_cast<std::uint32_t>(std::stoul(match[1].str(), nullptr, 16));
            address.bus = static_cast<std::uint32_t>(std::stoul(match[2].str(), nullptr, 16));
            address.device = static_cast<std::uint32_t>(std::stoul(match[3].str(), nullptr, 16));
            address.function = static_cast<std::uint32_t>(std::stoul(match[4].str(), nullptr, 16));
            return address;
        }

        HardwareDiscovery::HardwareDiscovery(DeviceEnumerator& enumerator, NUMAReader& reader) noexcept
            : enumerator_(enumerator), reader_(reader)
        {
        }

        Result<std::vector<HardwareNode>>
        HardwareDiscovery::probe_all_devices(const DeviceFilter& config)
        {
            SR_LOG_DEBUG(K_TOPOLOGY_LOG_CATEGORY, "Starting expanded hardware probe...");
            Result<std::vector<EnumeratedDevice>> listed = enumerator_.enumerate();
            if (!listed)
                return make_error(enumeration_error(config, listed.error().message.c_str()));
            if (!reader_.loaded())
                SR_LOG_WARN(K_TOPOLOGY_LOG_CATEGORY, "hwloc could not load the machine topology; CPU devices keep NUMA -1 "
                            "and a GPU's PCI address comes from SYCL alone.");

            std::vector<HardwareNode> nodes;

            // At most one CPU is kept per NUMA node (modules/platform/topology/README.md).
            std::unordered_set<int> seen_cpu_numa;

            // At most one GPU or accelerator is kept per PCI address (modules/platform/topology/README.md).
            std::set<PCIKey> seen_pci;

            for (EnumeratedDevice& entry : *listed)
            {
                if (!admits_class(config, entry.kind)) continue;

                // fp64 is checked at discovery, not at kernel launch (modules/platform/topology/README.md).
                if (!admits(config, entry.kind, entry.fp64))
                {
                    SR_LOG_INFO(K_TOPOLOGY_LOG_CATEGORY, "Skipping device [{}]: no fp64 support and require_fp64 is set.",
                                entry.name);
                    continue;
                }

                HardwareNode node;
                node.logical_id = nodes.size();
                node.sycl_dev = entry.device;
                node.name = std::move(entry.name);
                node.is_gpu = entry.kind == DeviceKind::GPU;
                node.global_mem_size = entry.global_mem_size;
                node.max_compute_units = entry.max_compute_units;

                if (entry.kind == DeviceKind::CPU)
                {
                    // The CPU device takes the node of the first PU; -1 stays when none is named.
                    node.numa_node_id = reader_.cpu_numa_node().value_or(-1);

                    // A second CPU on one NUMA node is dropped (modules/platform/topology/README.md).
                    if (!seen_cpu_numa.insert(node.numa_node_id).second)
                    {
                        SR_LOG_INFO(K_TOPOLOGY_LOG_CATEGORY, "Skipping duplicate CPU device [{}] on NUMA {} (already mapped).",
                                    node.name, node.numa_node_id);
                        continue;
                    }
                }
                else if (entry.kind == DeviceKind::GPU || entry.kind == DeviceKind::Accelerator)
                {
                    // The hwloc PCI tree, by GPU name, when the driver names no address (README.md).
                    std::string pci_text = std::move(entry.pci_text);
                    if (pci_text.empty())
                        pci_text = reader_.gpu_pci_address(node.name);
                    if (!pci_text.empty())
                    {
                        node.pci_address = parse_pci_address(pci_text, node.name);
                        if (node.pci_address.has_value())
                            SR_LOG_DEBUG(K_TOPOLOGY_LOG_CATEGORY, "Device {} PCI: {}", node.name,
                                         describe(*node.pci_address));
                    }
                }

                // A second device at one PCI address is dropped (modules/platform/topology/README.md).
                if (node.pci_address.has_value() &&
                    !seen_pci.insert(pci_key(*node.pci_address)).second)
                {
                    SR_LOG_INFO(K_TOPOLOGY_LOG_CATEGORY, "Skipping duplicate device [{}] at PCI {} (already mapped).",
                                node.name, describe(*node.pci_address));
                    continue;
                }

                nodes.push_back(std::move(node));
            }

            SR_LOG_INFO(K_TOPOLOGY_LOG_CATEGORY, "Expanded probe complete. {} nodes found.", nodes.size());
            return nodes;
        }

    } // namespace Topology
} // namespace SushiRuntime