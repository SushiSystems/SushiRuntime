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
 * @see modules/platform/topology/README.md
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief PCI Bus-Device-Function (BDF) address.
         * 
         * Used to map SYCL devices to NUMA nodes using hwloc.
         */
        struct PCIDomainAddress 
        {
            std::uint32_t domain = 0;   /**< PCI domain */
            std::uint32_t bus = 0;      /**< PCI bus */
            std::uint32_t device = 0;   /**< PCI device */
            std::uint32_t function = 0; /**< PCI function */
        };

        /**
         * @brief Represents a hardware unit like CPU or GPU.
         */
        struct HardwareNode
        {
            std::size_t logical_id = 0; /**< Logical ID within SushiRuntime. */
            int numa_node_id = -1;      /**< ID of the NUMA node from hwloc; -1 until mapped. */
            sycl::device sycl_dev{};    /**< SYCL device object. */
            std::string name{};         /**< Name of the device. */
            bool is_gpu = false;        /**< True if the device is a GPU. */

            /** @brief Holds the PCI address; empty when no source reported one. */
            std::optional<PCIDomainAddress> pci_address = std::nullopt;

            std::size_t global_mem_size = 0;     /**< Total global memory size in bytes. */
            std::uint32_t max_compute_units = 0; /**< Total number of compute units. */
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
             * @see modules/platform/topology/README.md
             */
            bool require_fp64 = false;
        };

        /**
         * @brief Enumerates available SYCL compute devices and maps each to its NUMA node.
         *
         * Used once at RuntimeContext construction to build the device list. At most one
         * CPU is kept per NUMA node, and at most one GPU or accelerator per resolved PCI
         * address, the first enumerated; a device with no PCI address is always kept.
         */
        class SUSHIRUNTIME_API HardwareDiscovery
        {
            public:
                HardwareDiscovery() = default;

                /**
                 * @brief Scans all SYCL platforms and returns matching hardware nodes.
                 * @param config Device-class filter; defaults to including all classes.
                 * @return The devices kept, with logical IDs and NUMA assignments, or
                 *         Unsupported when SYCL could not list its devices.
                 */
                [[nodiscard]] Result<std::vector<HardwareNode>>
                probe_all_devices(const HardwareFilterConfig& config = {});

            private:
                /**
                 * @brief Scans SYCL platforms to find devices.
                 * @return Nothing, or Unsupported when SYCL could not list its devices.
                 */
                [[nodiscard]] Result<void> discover_sycl_devices(std::vector<HardwareNode>& nodes,
                                                                 const HardwareFilterConfig& config);
        };

    } // namespace Topology
} // namespace SushiRuntime