/**************************************************************************/
/* device_filter.hpp                                                      */
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
 * @file device_filter.hpp
 * @brief Declares DeviceFilter, the device classes and features a runtime accepts.
 * @author Mustafa Garip
 * @see modules/foundation/machine/README.md
 */

#pragma once

namespace SushiRuntime
{
    /**
     * @brief Names the device classes and features a runtime accepts.
     */
    struct DeviceFilter
    {
        bool include_cpu = true;         /**< Accepts CPU devices. */
        bool include_gpu = true;         /**< Accepts GPU devices. */
        bool include_accelerator = true; /**< Accepts accelerators such as FPGAs. */

        /**
         * @brief Drops devices that cannot execute double-precision arithmetic; off by default.
         * @see modules/foundation/machine/README.md
         */
        bool require_fp64 = false;
    };
} // namespace SushiRuntime
