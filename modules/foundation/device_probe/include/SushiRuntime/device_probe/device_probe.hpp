/**************************************************************************/
/* device_probe.hpp                                                       */
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
 * @file device_probe.hpp
 * @brief Declares the query that tells whether SYCL reports any device.
 * @author Mustafa Garip
 */

#pragma once

#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Returns whether SYCL reports at least one device of any type.
         *
         * Constructs no SushiRuntime object. An exception from SYCL device discovery
         * propagates to the caller, so a broken installation fails the test.
         */
        [[nodiscard]] inline bool has_any_sycl_device()
        {
            return !sycl::device::get_devices().empty();
        }
    } // namespace Test
} // namespace SushiRuntime
