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
 * @brief Filters the enumerated devices, resolves each one's NUMA node or PCI address and names it.
 * @author Mustafa Garip
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <optional>
#include <regex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/topology/device_key_assigner.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/physical_card_selector.hpp>
#include <SushiRuntime/topology/topology_log_category.hpp>
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

            /** @brief Holds one device the filter admitted, with what its key is made from. */
            struct Candidate
            {
                /// @brief The node; its logical id and key are set last.
                HardwareNode node;
                /// @brief What names the device.
                DeviceIdentity identity;
            };

            /** @brief Returns whether the filter admits the device; logs a device dropped for fp64. */
            bool admitted(const DeviceFilter& config, const DeviceDescriptor& entry)
            {
                if (!admits_class(config, entry.kind))
                    return false;

                // fp64 is checked at discovery, not at kernel launch (modules/platform/topology/README.md).
                if (!admits(config, entry.kind, entry.fp64))
                {
                    SR_LOG_INFO(K_TOPOLOGY_LOG_CATEGORY,
                                "Skipping device [{}]: no fp64 support and require_fp64 is set.",
                                entry.name);
                    return false;
                }
                return true;
            }

            /** @brief Builds the node of an admitted device, without its NUMA node, address or key. */
            HardwareNode node_of(DeviceDescriptor& entry)
            {
                HardwareNode node;
                node.device_id = entry.id;
                node.kind = entry.kind;
                node.name = std::move(entry.name);
                node.is_gpu = entry.kind == DeviceKind::GPU;
                node.global_mem_size = entry.global_mem_size;
                node.max_compute_units = entry.max_compute_units;
                return node;
            }

            /**
             * @brief Gives a CPU the NUMA node of the reader and says whether to keep it.
             *
             * A second CPU on one NUMA node is dropped (modules/platform/topology/README.md).
             */
            bool keeps_cpu(HardwareNode& node, NUMAReader& reader, std::unordered_set<int>& seen_numa)
            {
                // The CPU device takes the node of the first PU; -1 stays when none is named.
                node.numa_node_id = reader.cpu_numa_node().value_or(-1);
                if (seen_numa.insert(node.numa_node_id).second)
                    return true;

                SR_LOG_INFO(K_TOPOLOGY_LOG_CATEGORY,
                            "Skipping duplicate CPU device [{}] on NUMA {} (already mapped).",
                            node.name, node.numa_node_id);
                return false;
            }

            /** @brief Sets the PCI address of a GPU or an accelerator from the driver, then from hwloc. */
            void resolve_pci(DeviceDescriptor& entry, HardwareNode& node, NUMAReader& reader)
            {
                // The hwloc PCI tree, by GPU name, when the driver names no address (README.md).
                std::string pci_text = std::move(entry.pci_text);
                if (pci_text.empty())
                    pci_text = reader.gpu_pci_address(node.name);
                if (pci_text.empty())
                    return;

                node.pci_address = parse_pci_address(pci_text, node.name);
                if (node.pci_address.has_value())
                    SR_LOG_DEBUG(K_TOPOLOGY_LOG_CATEGORY, "Device {} PCI: {}", node.name,
                                 describe(*node.pci_address));
            }

            /** @brief Returns what names the device of a descriptor and its resolved node. */
            DeviceIdentity identity_of(const DeviceDescriptor& entry, const HardwareNode& node)
            {
                DeviceIdentity identity;
                identity.backend = entry.backend;
                identity.vendor = entry.vendor;
                identity.name = node.name;
                identity.uuid = entry.uuid;
                identity.pci_address = node.pci_address;
                identity.global_mem_size = node.global_mem_size;
                identity.max_compute_units = node.max_compute_units;
                return identity;
            }

            /** @brief Returns the identity of every candidate, in order. */
            std::vector<DeviceIdentity> identities_of(const std::vector<Candidate>& candidates)
            {
                std::vector<DeviceIdentity> identities;
                identities.reserve(candidates.size());
                for (const Candidate& candidate : candidates)
                    identities.push_back(candidate.identity);
                return identities;
            }

            /** @brief Keeps one candidate per physical card and logs each one dropped. */
            std::vector<Candidate> one_per_card(std::vector<Candidate> candidates)
            {
                const std::vector<std::size_t> kept_positions =
                    select_physical_cards(identities_of(candidates));

                std::vector<Candidate> kept;
                kept.reserve(kept_positions.size());
                std::size_t next_kept = 0;
                for (std::size_t position = 0; position < candidates.size(); ++position)
                {
                    if (next_kept < kept_positions.size() && kept_positions[next_kept] == position)
                    {
                        kept.push_back(std::move(candidates[position]));
                        ++next_kept;
                        continue;
                    }
                    SR_LOG_INFO(K_TOPOLOGY_LOG_CATEGORY,
                                "Skipping duplicate device [{}]: another backend reports the "
                                "same card (already mapped).",
                                candidates[position].node.name);
                }
                return kept;
            }

            /** @brief Gives each candidate its key, sorts them by it and numbers them from zero. */
            std::vector<HardwareNode> keyed_in_order(std::vector<Candidate> candidates)
            {
                const std::vector<DeviceKey> keys =
                    DeviceKeyAssigner::standard().assign(identities_of(candidates));

                std::vector<std::size_t> order(candidates.size());
                std::iota(order.begin(), order.end(), std::size_t{0});
                std::sort(order.begin(), order.end(), [&keys](std::size_t left, std::size_t right)
                          { return keys[left] < keys[right]; });

                std::vector<HardwareNode> nodes;
                nodes.reserve(candidates.size());
                for (const std::size_t position : order)
                {
                    HardwareNode node = std::move(candidates[position].node);
                    node.key = keys[position];
                    node.logical_id = nodes.size();
                    nodes.push_back(std::move(node));
                }
                return nodes;
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
            Result<std::vector<DeviceDescriptor>> listed = enumerator_.enumerate();
            if (!listed)
                return make_error(enumeration_error(config, listed.error().message.c_str()));
            if (!reader_.loaded())
                SR_LOG_WARN(K_TOPOLOGY_LOG_CATEGORY, "hwloc could not load the machine topology; CPU devices keep NUMA -1 "
                            "and a GPU's PCI address comes from SYCL alone.");

            std::vector<Candidate> candidates;
            std::unordered_set<int> seen_cpu_numa;
            for (DeviceDescriptor& entry : *listed)
            {
                if (!admitted(config, entry))
                    continue;

                HardwareNode node = node_of(entry);
                if (entry.kind == DeviceKind::CPU)
                {
                    if (!keeps_cpu(node, reader_, seen_cpu_numa))
                        continue;
                }
                else if (entry.kind == DeviceKind::GPU || entry.kind == DeviceKind::Accelerator)
                {
                    resolve_pci(entry, node, reader_);
                }

                DeviceIdentity identity = identity_of(entry, node);
                candidates.push_back(Candidate{std::move(node), std::move(identity)});
            }

            std::vector<HardwareNode> nodes = keyed_in_order(one_per_card(std::move(candidates)));
            SR_LOG_INFO(K_TOPOLOGY_LOG_CATEGORY, "Expanded probe complete. {} nodes found.", nodes.size());
            return nodes;
        }

    } // namespace Topology
} // namespace SushiRuntime
