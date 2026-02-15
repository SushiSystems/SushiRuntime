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
#include <sycl/sycl.hpp>
#include <vector>
#include <string>

namespace SushiRuntime
{
    namespace Topology
    {
        /**
         * @brief pci bus-device-function (bdf) address.
         * 
         * used to map sycl devices to numa nodes using hwloc.
         */
        struct PCIDomainAddress 
        {
            uint32_t domain;   /**< pci domain */
            uint32_t bus;      /**< pci bus */
            uint32_t device;   /**< pci device */
            uint32_t function; /**< pci function */
        };

        /**
         * @brief represents a hardware unit like cpu or gpu.
         */
        struct HardwareNode 
        {
            size_t logical_id;          /**< logical id within sushiruntime. */
            int numa_node_id;           /**< id of the numa node from hwloc. */
            sycl::device sycl_dev;      /**< sycl device object. */
            std::string name;           /**< name of the device. */
            bool is_gpu;                /**< true if the device is a gpu. */
            
            PCIDomainAddress pci_address; /**< pci address of the device. */
            
            size_t global_mem_size;       /**< total global memory size in bytes. */
            uint32_t max_compute_units;   /**< total number of compute units. */
        };

        /**
         * @brief class to find all hardware in the system.
         */
        class HardwareDiscovery 
        {
            public:
                HardwareDiscovery() = default;

                /**
                 * @brief scans for all sycl devices and returns a list of hardware nodes.
                 * @return std::vector<HardwareNode> list of found hardware.
                 */
                std::vector<HardwareNode> probe_all_devices();

            private:
                /** @brief scans sycl platforms to find devices. */
                void discover_sycl_devices(std::vector<HardwareNode>& nodes);
        };

    } // namespace Topology
} // namespace SushiRuntime