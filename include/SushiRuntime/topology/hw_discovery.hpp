/**************************************************************************/
/* hw_discovery.hpp                                                       */
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
 * @file hw_discovery.hpp
 * @brief Declares HardwareDiscovery and the hardware node and filter types.
 * @author Mustafa Garip
 * @see include/SushiRuntime/topology/README.md
 */

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
         * @brief Configuration for filtering the hardware topology discovery.
         */
        struct HardwareFilterConfig
        {
            bool include_cpu = true;
            bool include_gpu = true;
            bool include_accelerator = true;

            /**
             * @brief Drops devices that cannot execute double-precision arithmetic; off by default.
             * @see include/SushiRuntime/topology/README.md
             */
            bool require_fp64 = false;
        };

        /**
         * @brief Enumerates available SYCL compute devices and maps each to its NUMA node.
         *
         * Used once at RuntimeContext construction to build the device list. Duplicate
         * SYCL devices (same platform/device reported by multiple backends) are
         * deduplicated before the list is returned.
         */
        class SUSHIRUNTIME_API HardwareDiscovery
        {
            public:
                HardwareDiscovery() = default;

                /**
                 * @brief Scans all SYCL platforms and returns matching hardware nodes.
                 * @param config Device-class filter; defaults to including all classes.
                 * @return Deduplicated list of found devices with logical IDs and NUMA assignments.
                 */
                std::vector<HardwareNode> probe_all_devices(const HardwareFilterConfig& config = {});

            private:
                /** @brief Scans SYCL platforms to find devices. */
                void discover_sycl_devices(std::vector<HardwareNode>& nodes, const HardwareFilterConfig& config);
        };

    } // namespace Topology
} // namespace SushiRuntime