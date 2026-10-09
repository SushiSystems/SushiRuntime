/**************************************************************************/
/* device_info.hpp                                                        */
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
 * @file device_info.hpp
 * @brief Declares DeviceKind, DeviceInfo and DeviceDescriptor, the inventory record of a device.
 * @author Mustafa Garip
 * @see include/SushiRuntime/device/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Device
    {
        /**
         * @brief Names the class of a compute device.
         */
        enum class DeviceKind : std::uint8_t
        {
            Cpu,         /**< A CPU device. */
            Gpu,         /**< A GPU device. */
            Accelerator, /**< An accelerator such as an FPGA. */
            Other        /**< Any device SYCL reports under none of the classes above. */
        };

        /**
         * @brief Describes one device of the inventory.
         */
        struct DeviceInfo
        {
            std::size_t index = 0;                         /**< Position in the device set. */
            std::string name;                              /**< Name the device reports. */
            DeviceKind kind = DeviceKind::Other;           /**< Class of the device. */
            sycl::backend backend = sycl::backend::opencl; /**< SYCL backend that drives it. */
            int numa_node = 0;                             /**< NUMA node the device sits on. */
            std::uint64_t memory_bytes = 0;                /**< Global memory of the device. */
            std::uint32_t compute_units = 0;               /**< Parallel compute units it has. */
        };

        /**
         * @brief Pairs a SYCL device with its inventory record.
         */
        struct DeviceDescriptor
        {
            sycl::device device; /**< The SYCL device a queue is built on. */
            DeviceInfo info;     /**< What the inventory reports about it. */
        };
    } // namespace Device
} // namespace SushiRuntime
