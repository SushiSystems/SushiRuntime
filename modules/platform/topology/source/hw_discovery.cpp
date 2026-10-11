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
 * @file hw_discovery.cpp
 * @brief Enumerates SYCL devices and resolves each one's NUMA node or PCI address.
 * @author Mustafa Garip
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <cstdio>
#include <hwloc.h>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include "numa_lookup.hpp"

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        namespace
        {
            /** @brief Loads the machine's hwloc topology and destroys it on scope exit. */
            class LoadedHWLOCTopology final
            {
                public:
                    /** @brief Loads the topology; get() returns null when hwloc refuses. */
                    LoadedHWLOCTopology() noexcept
                    {
                        if (hwloc_topology_init(&topology_) < 0)
                        {
                            topology_ = nullptr;
                            return;
                        }
                        hwloc_topology_set_flags(topology_, HWLOC_TOPOLOGY_FLAG_INCLUDE_DISALLOWED);
                        hwloc_topology_set_io_types_filter(topology_, HWLOC_TYPE_FILTER_KEEP_ALL);
                        if (hwloc_topology_load(topology_) < 0)
                        {
                            hwloc_topology_destroy(topology_);
                            topology_ = nullptr;
                        }
                    }

                    /** @brief Destroys the topology, if one was loaded. */
                    ~LoadedHWLOCTopology()
                    {
                        if (topology_ != nullptr)
                            hwloc_topology_destroy(topology_);
                    }

                    LoadedHWLOCTopology(const LoadedHWLOCTopology&) = delete;
                    LoadedHWLOCTopology& operator=(const LoadedHWLOCTopology&) = delete;
                    LoadedHWLOCTopology(LoadedHWLOCTopology&&) = delete;
                    LoadedHWLOCTopology& operator=(LoadedHWLOCTopology&&) = delete;

                    /** @brief Returns the loaded topology, or null when hwloc refused. */
                    [[nodiscard]] hwloc_topology_t get() const noexcept { return topology_; }

                private:
                    /** @brief Holds the loaded topology, or null. */
                    hwloc_topology_t topology_ = nullptr;
            };

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

        /**
         * @brief Discovers the SYCL devices that pass config and returns them as hardware nodes.
         * @param config The filter configuration to use during the probe.
         * @return The discovered hardware nodes, or Unsupported when SYCL could not list them.
         */
        Result<std::vector<HardwareNode>>
        HardwareDiscovery::probe_all_devices(const DeviceFilter& config)
        {
            SR_LOG_DEBUG("Starting expanded hardware probe...");
            std::vector<HardwareNode> discovered_nodes;

            Result<void> discovered = discover_sycl_devices(discovered_nodes, config);
            if (!discovered)
                return make_error(std::move(discovered.error()));

            SR_LOG_INFO("Expanded probe complete. {} nodes found.", discovered_nodes.size());
            return discovered_nodes;
        }

        /**
         * @brief Scans all SYCL devices and extracts their physical and capability details.
         * @param nodes The vector to populate with discovered devices.
         * @param config The filter configuration specifying which device types to include.
         * @return Nothing, or Unsupported when SYCL could not list its devices.
         */
        Result<void> HardwareDiscovery::discover_sycl_devices(std::vector<HardwareNode>& nodes,
                                                              const DeviceFilter& config)
        {
            const LoadedHWLOCTopology loaded_topology;
            const hwloc_topology_t topology = loaded_topology.get();
            if (topology == nullptr)
                SR_LOG_WARN("hwloc could not load the machine topology; CPU devices keep NUMA -1 "
                            "and a GPU's PCI address comes from SYCL alone.");

            try
            {
                size_t logical_id_counter = 0;

                // At most one CPU is kept per NUMA node (modules/platform/topology/README.md).
                std::unordered_set<int> seen_cpu_numa;

                // At most one GPU or accelerator is kept per PCI address (modules/platform/topology/README.md).
                std::set<PCIKey> seen_pci;

                for (const auto& dev : sycl::device::get_devices(sycl::info::device_type::all))
                {
                    if (dev.is_cpu() && !config.include_cpu) continue;
                    if (dev.is_gpu() && !config.include_gpu) continue;
                    if (dev.is_accelerator() && !config.include_accelerator) continue;

                    // fp64 is checked at discovery, not at kernel launch (modules/platform/topology/README.md).
                    if (config.require_fp64 && !dev.has(sycl::aspect::fp64))
                    {
                        SR_LOG_INFO("Skipping device [{}]: no fp64 support and require_fp64 is set.",
                                    dev.get_info<sycl::info::device::name>());
                        continue;
                    }

                    HardwareNode node;
                    node.logical_id = logical_id_counter++;
                    node.sycl_dev = dev;
                    node.name = dev.get_info<sycl::info::device::name>();
                    node.is_gpu = dev.is_gpu();
                    node.numa_node_id = -1;

                    /** @brief Retrieve capacity and memory info. */
                    node.global_mem_size = dev.get_info<sycl::info::device::global_mem_size>();
                    node.max_compute_units = dev.get_info<sycl::info::device::max_compute_units>();

                    if (dev.is_cpu())
                    {
                        // The CPU device takes the node of the first PU; -1 stays when hwloc names none.
                        if (topology != nullptr)
                        {
                            const std::optional<unsigned> local_node = local_numa_os_index(
                                topology, hwloc_get_obj_by_type(topology, HWLOC_OBJ_PU, 0));
                            if (local_node.has_value())
                                node.numa_node_id = static_cast<int>(*local_node);
                        }

                        // A second CPU on one NUMA node is dropped (modules/platform/topology/README.md).
                        if (!seen_cpu_numa.insert(node.numa_node_id).second)
                        {
                            SR_LOG_INFO("Skipping duplicate CPU device [{}] on NUMA {} (already mapped).",
                                        node.name, node.numa_node_id);
                            --logical_id_counter;
                            continue;
                        }
                    }
                    else if (node.is_gpu || dev.is_accelerator())
                    {
                        try
                        {
                            std::string pci_str = "";

                            // Primary source: Intel device-info extension (modules/platform/topology/README.md).
#if defined(SYCL_EXT_INTEL_DEVICE_INFO) && SYCL_EXT_INTEL_DEVICE_INFO >= 5
                            const bool has_intel_pci = dev.has(sycl::aspect::ext_intel_pci_address);
#else
                            constexpr bool has_intel_pci = false;
#endif
                            if (has_intel_pci)
                            {
#if defined(SYCL_EXT_INTEL_DEVICE_INFO) && SYCL_EXT_INTEL_DEVICE_INFO >= 5
                                pci_str = dev.get_info<sycl::ext::intel::info::device::pci_address>();
#endif
                            }
                            else
                            {
                                // Fallback 1: regex scan in vendor string (catches some Codeplay/NVIDIA combos)
                                try
                                {
                                    std::string vendor = dev.get_info<sycl::info::device::vendor>();
                                    std::regex pci_regex("([0-9a-fA-F]{4}):([0-9a-fA-F]{2}):([0-9a-fA-F]{2})\\.([0-9a-fA-F])");
                                    std::smatch match;
                                    if (std::regex_search(vendor, match, pci_regex))
                                        pci_str = match.str();
                                } catch (...) {}

                                // Fallback 2: hwloc PCI tree, by GPU name (modules/platform/topology/README.md).
                                if (pci_str.empty() && topology != nullptr)
                                {
                                    std::string dev_name = node.name;
                                    hwloc_obj_t osdev = nullptr;
                                    while ((osdev = hwloc_get_next_osdev(topology, osdev)) != nullptr)
                                    {
                                        if (osdev->attr->osdev.type != HWLOC_OBJ_OSDEV_GPU)
                                            continue;
                                        if (osdev->name && dev_name.find(osdev->name) != std::string::npos)
                                        {
                                            // Walk up to the PCI object to get the BDF address
                                            hwloc_obj_t pci_obj = hwloc_get_non_io_ancestor_obj(topology, osdev);
                                            hwloc_obj_t io = osdev->parent;
                                            while (io && io->type != HWLOC_OBJ_PCI_DEVICE)
                                                io = io->parent;
                                            if (io && io->type == HWLOC_OBJ_PCI_DEVICE)
                                            {
                                                char buf[32];
                                                std::snprintf(buf, sizeof(buf), "%04x:%02x:%02x.%x",
                                                    io->attr->pcidev.domain,
                                                    io->attr->pcidev.bus,
                                                    io->attr->pcidev.dev,
                                                    io->attr->pcidev.func);
                                                pci_str = buf;
                                                (void)pci_obj;
                                                SR_LOG_DEBUG("Device {} found via hwloc osdev scan: {}", dev_name, pci_str);
                                            }
                                            break;
                                        }
                                    }
                                }
                            }

                            if (!pci_str.empty())
                            {
                                std::regex pci_regex("([0-9a-fA-F]+):([0-9a-fA-F]+):([0-9a-fA-F]+)\\.([0-9a-fA-F]+)");
                                std::smatch match;
                                if (std::regex_search(pci_str, match, pci_regex) && match.size() == 5)
                                {
                                    // The node takes the address only once all four fields parsed.
                                    PCIDomainAddress address;
                                    address.domain = static_cast<std::uint32_t>(std::stoul(match[1].str(), nullptr, 16));
                                    address.bus = static_cast<std::uint32_t>(std::stoul(match[2].str(), nullptr, 16));
                                    address.device = static_cast<std::uint32_t>(std::stoul(match[3].str(), nullptr, 16));
                                    address.function = static_cast<std::uint32_t>(std::stoul(match[4].str(), nullptr, 16));
                                    node.pci_address = address;
                                    SR_LOG_DEBUG("Device {} PCI: {}", node.name, describe(address));
                                }
                            }
                        }
                        catch (...)
                        {
                            SR_LOG_WARN("Could not retrieve PCI address for {}, NUMA mapping may be degraded.", node.name);
                            node.pci_address.reset();
                        }
                    }

                    // A second device at one PCI address is dropped (modules/platform/topology/README.md).
                    if (node.pci_address.has_value() &&
                        !seen_pci.insert(pci_key(*node.pci_address)).second)
                    {
                        SR_LOG_INFO("Skipping duplicate device [{}] at PCI {} (already mapped).",
                                    node.name, describe(*node.pci_address));
                        --logical_id_counter;
                        continue;
                    }

                    nodes.push_back(node);
                }
            }
            catch (const sycl::exception& e)
            {
                return make_error(enumeration_error(config, e.what()));
            }
            return {};
        }

    } // namespace Topology
} // namespace SushiRuntime