/**************************************************************************/
/* hw_discovery.hpp                                                       */
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

#pragma once
#include <string>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /**
         * @brief PCI Bus-Device-Function (BDF) address.
         * 
         * Used to map SYCL devices to NUMA nodes using hwloc.
         */
        struct PCIDomainAddress 
        {
            uint32_t domain;   /**< PCI domain */
            uint32_t bus;      /**< PCI bus */
            uint32_t device;   /**< PCI device */
            uint32_t function; /**< PCI function */
        };

        /**
         * @brief Represents a hardware unit like CPU or GPU.
         */
        struct HardwareNode 
        {
            size_t logical_id;          /**< Logical ID within SushiRuntime. */
            int numa_node_id;           /**< ID of the NUMA node from hwloc. */
            sycl::device sycl_dev;      /**< SYCL device object. */
            std::string name;           /**< Name of the device. */
            bool is_gpu;                /**< True if the device is a GPU. */
            
            PCIDomainAddress pci_address; /**< PCI address of the device. */
            
            size_t global_mem_size;       /**< Total global memory size in bytes. */
            uint32_t max_compute_units;   /**< Total number of compute units. */
        };

        /**
         * @brief Class to find all hardware in the system.
         */
        class SUSHIRUNTIME_API HardwareDiscovery 
        {
            public:
                HardwareDiscovery() = default;

                /**
                 * @brief Scans for all SYCL devices and returns a list of hardware nodes.
                 * @return std::vector<HardwareNode> List of found hardware.
                 */
                std::vector<HardwareNode> probe_all_devices();

            private:
                /** @brief Scans SYCL platforms to find devices. */
                void discover_sycl_devices(std::vector<HardwareNode>& nodes);
        };

    } // namespace Topology
} // namespace SushiRuntime