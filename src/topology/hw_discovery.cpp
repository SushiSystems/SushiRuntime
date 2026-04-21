/**************************************************************************/
/* hw_discovery.cpp                                                       */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdio>
#include <hwloc.h>
#include <regex>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /** @brief Triggers an expanded hardware probe to find all compute units. */
        std::vector<HardwareNode> HardwareDiscovery::probe_all_devices(const HardwareFilterConfig& config) 
        {
            SR_LOG_DEBUG("Starting expanded hardware probe...");
            std::vector<HardwareNode> discovered_nodes;

            discover_sycl_devices(discovered_nodes, config);

            SR_LOG_INFO("Expanded probe complete. {} nodes found.", discovered_nodes.size());
            return discovered_nodes;
        }

        /** @brief Scans all SYCL devices and extracts their physical and capability details. */
        void HardwareDiscovery::discover_sycl_devices(std::vector<HardwareNode>& nodes, const HardwareFilterConfig& config) 
        {
            hwloc_topology_t topology;
            hwloc_topology_init(&topology);
            hwloc_topology_set_flags(topology, HWLOC_TOPOLOGY_FLAG_INCLUDE_DISALLOWED);
            hwloc_topology_load(topology);

            try 
            {
                size_t logical_id_counter = 0;

                for (const auto& dev : sycl::device::get_devices(sycl::info::device_type::all)) 
                {
                    if (dev.is_cpu() && !config.include_cpu) continue;
                    if (dev.is_gpu() && !config.include_gpu) continue;
                    if (dev.is_accelerator() && !config.include_accelerator) continue;

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
                    }
                    else if (node.is_gpu || dev.is_accelerator()) 
                    {
                        try 
                        {
                            /** @brief Try to extract PCI address for NUMA mapping. */
                            std::string pci_str = "";
                            if (dev.has(sycl::aspect::ext_intel_pci_address))
                                pci_str = dev.get_info<sycl::ext::intel::info::device::pci_address>();
                            else 
                            {
                                // Multi-vendor PCI probe using common property strings (e.g., Codeplay, NVML fallbacks).
                                // Try finding PCI info within device vendor string or custom Codeplay extensions if standard fails.
                                try 
                                {
                                    std::string dev_version = dev.get_info<sycl::info::device::vendor>();
                                    // Use regex to find potential PCI formatted string in vendor/name as fallback
                                    std::regex pci_regex("([0-9a-fA-F]{4}):([0-9a-fA-F]{2}):([0-9a-fA-F]{2})\\.([0-9a-fA-F])");
                                    std::smatch match;
                                    if (std::regex_search(dev_version, match, pci_regex)) {
                                        pci_str = match.str();
                                    }
                                } catch (...) {}
                            }
 
                            if (!pci_str.empty()) 
                            {
                                /** @brief Parse PCI address string (format: 0000:00:02.0). */
                                std::regex pci_regex("([0-9a-fA-F]+):([0-9a-fA-F]+):([0-9a-fA-F]+)\\.([0-9a-fA-F]+)");
                                std::smatch match;
                                if (std::regex_search(pci_str, match, pci_regex) && match.size() == 5) 
                                {
                                    node.pci_address.domain = std::stoul(match[1].str(), nullptr, 16);
                                    node.pci_address.bus = std::stoul(match[2].str(), nullptr, 16);
                                    node.pci_address.device = std::stoul(match[3].str(), nullptr, 16);
                                    node.pci_address.function = std::stoul(match[4].str(), nullptr, 16);
                                    
                                    SR_LOG_DEBUG("Device {} PCI: {:04x}:{:02x}:{:02x}.{:x}", 
                                                node.name, node.pci_address.domain, node.pci_address.bus, 
                                                node.pci_address.device, node.pci_address.function);
                                }
                            }
                        } 
                        catch (...) 
                        {
                            SR_LOG_WARN("Could not retrieve PCI address for {}, NUMA mapping may be degraded.", node.name);
                            node.pci_address = {0,0,0,0};
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