/**************************************************************************/
/* device_kind.hpp                                                        */
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
 * @file device_kind.hpp
 * @brief Declares DeviceKind, the class of a compute device.
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

namespace SushiRuntime
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
} // namespace SushiRuntime
