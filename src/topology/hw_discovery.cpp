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
#include <cstdio>
#include <hwloc.h>
#include <regex>
#include <unordered_set>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /** 
         * @brief Triggers an expanded hardware probe to find all compute units. 
         * @param config The filter configuration to use during the probe.
         * @return A vector of discovered hardware nodes.
         */
        std::vector<HardwareNode> HardwareDiscovery::probe_all_devices(const HardwareFilterConfig& config) 
        {
            SR_LOG_DEBUG("Starting expanded hardware probe...");
            std::vector<HardwareNode> discovered_nodes;

            discover_sycl_devices(discovered_nodes, config);

            SR_LOG_INFO("Expanded probe complete. {} nodes found.", discovered_nodes.size());
            return discovered_nodes;
        }

        /** 
         * @brief Scans all SYCL devices and extracts their physical and capability details. 
         * @param nodes The vector to populate with discovered devices.
         * @param config The filter configuration specifying which device types to include.
         */
        void HardwareDiscovery::discover_sycl_devices(std::vector<HardwareNode>& nodes, const HardwareFilterConfig& config) 
        {
            hwloc_topology_t topology;
            hwloc_topology_init(&topology);
            hwloc_topology_set_flags(topology, HWLOC_TOPOLOGY_FLAG_INCLUDE_DISALLOWED);
            hwloc_topology_set_io_types_filter(topology, HWLOC_TYPE_FILTER_KEEP_ALL);
            hwloc_topology_load(topology);

            try 
            {
                size_t logical_id_counter = 0;

                // At most one CPU is kept per NUMA node (src/topology/README.md).
                std::unordered_set<int> seen_cpu_numa;

                for (const auto& dev : sycl::device::get_devices(sycl::info::device_type::all))
                {
                    if (dev.is_cpu() && !config.include_cpu) continue;
                    if (dev.is_gpu() && !config.include_gpu) continue;
                    if (dev.is_accelerator() && !config.include_accelerator) continue;

                    // fp64 is checked at discovery, not at kernel launch (src/topology/README.md).
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
                        // Use HWLOC to find NUMA node for CPU
                        hwloc_obj_t pu = hwloc_get_obj_by_type(topology, HWLOC_OBJ_PU, 0);
                        if (pu) {
                            hwloc_obj_t numa = hwloc_get_ancestor_obj_by_type(topology, HWLOC_OBJ_NUMANODE, pu);
                            if (numa) node.numa_node_id = numa->os_index;
                        }

                        // A second CPU on one NUMA node is dropped (src/topology/README.md).
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

                            // Primary source: Intel device-info extension (src/topology/README.md).
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

                                // Fallback 2: hwloc PCI tree, by GPU name (src/topology/README.md).
                                if (pci_str.empty())
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

                                    char pci_str[24];
                                    std::snprintf(pci_str, sizeof(pci_str), "%04x:%02x:%02x.%x",
                                                  static_cast<unsigned>(address.domain),
                                                  static_cast<unsigned>(address.bus),
                                                  static_cast<unsigned>(address.device),
                                                  static_cast<unsigned>(address.function));
                                    SR_LOG_DEBUG("Device {} PCI: {}", node.name, pci_str);
                                }
                            }
                        }
                        catch (...)
                        {
                            SR_LOG_WARN("Could not retrieve PCI address for {}, NUMA mapping may be degraded.", node.name);
                            node.pci_address.reset();
                        }
                    }

                    nodes.push_back(node);
                }
            }
            catch (const sycl::exception& e) 
            {
                hwloc_topology_destroy(topology);
                SR_LOG_ERROR("SYCL exception: {}", e.what());
                throw std::runtime_error("hardware discovery failed.");
            }
            hwloc_topology_destroy(topology);
        }
        
    } // namespace Topology
} // namespace SushiRuntime