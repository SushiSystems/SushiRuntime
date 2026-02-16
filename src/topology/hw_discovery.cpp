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

#include <cstdio>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /** @brief Triggers an expanded hardware probe to find all compute units. */
        std::vector<HardwareNode> HardwareDiscovery::probe_all_devices() 
        {
            SR_LOG_DEBUG("Starting expanded hardware probe...");
            std::vector<HardwareNode> discovered_nodes;

            discover_sycl_devices(discovered_nodes);

            SR_LOG_INFO("Expanded probe complete. {} nodes found.", discovered_nodes.size());
            return discovered_nodes;
        }

        /** @brief Scans all SYCL devices and extracts their physical and capability details. */
        void HardwareDiscovery::discover_sycl_devices(std::vector<HardwareNode>& nodes) 
        {
            try 
            {
                size_t logical_id_counter = 0;

                // TODO: allow user to filter devices (e.g. only gpus) via configuration.
                for (const auto& dev : sycl::device::get_devices(sycl::info::device_type::all)) 
                {
                    HardwareNode node;
                    node.logical_id = logical_id_counter++;
                    node.sycl_dev = dev;
                    node.name = dev.get_info<sycl::info::device::name>();
                    node.is_gpu = dev.is_gpu();
                    node.numa_node_id = -1; 
                    
                    /** @brief Retrieve capacity and memory info. */
                    node.global_mem_size = dev.get_info<sycl::info::device::global_mem_size>();
                    node.max_compute_units = dev.get_info<sycl::info::device::max_compute_units>();

                    if (node.is_gpu) 
                    {
                        try 
                        {
                            /** @brief Try to extract PCI address for NUMA mapping. */
                            std::string pci_str = "";
                            if (dev.has(sycl::aspect::ext_intel_pci_address)) {
                                pci_str = dev.get_info<sycl::ext::intel::info::device::pci_address>();
                            } else {
                                SR_LOG_WARN("Device {} does not support Intel PCI address extension.", node.name);
                            }
 
                            if (!pci_str.empty()) 
                            {
                                /** @brief Parse PCI address string (format: 0000:00:02.0). */
                                // TODO: replace sscanf with a safer parsing method or std::regex.
                                sscanf(pci_str.c_str(), "%x:%x:%x.%x", 
                                    &node.pci_address.domain, 
                                    &node.pci_address.bus, 
                                    &node.pci_address.device, 
                                    &node.pci_address.function);
                                
                                SR_LOG_DEBUG("Device {} PCI: {:04x}:{:02x}:{:02x}.{:x}", 
                                            node.name, node.pci_address.domain, node.pci_address.bus, 
                                            node.pci_address.device, node.pci_address.function);
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
                SR_LOG_ERROR("SYCL exception: {}", e.what());
                throw std::runtime_error("hardware discovery failed.");
            }
        }
        
    } // namespace Topology
} // namespace SushiRuntime