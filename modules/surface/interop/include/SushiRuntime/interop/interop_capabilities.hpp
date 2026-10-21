/**************************************************************************/
/* interop_capabilities.hpp                                               */
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
 * @file interop_capabilities.hpp
 * @brief Reports what one device can offer a memory pairing.
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#pragma once

#include <optional>

#include <SushiRuntime/interop/device_uuid.hpp>

namespace SushiRuntime
{
    /**
     * @brief Reports what one device can offer a memory pairing.
     *
     * See docs/design/SYCL_VULKAN_INTEROP/README.md §3.2 in the SushiEngine repository.
     */
    struct InteropCapabilities
    {
        /** @brief Reports whether the device's queue offers host USM. */
        bool host_export = false;
        /**
         * @brief Reports whether import_device can import on the device.
         *
         * True only when the toolchain has the bindless-images extension, the device is a
         * GPU, it reports the external-memory-import aspect and device_uuid holds a value.
         */
        bool device_import = false;
        /** @brief Holds the device's UUID, when it has one. */
        std::optional<DeviceUUID> device_uuid;
    };
} // namespace SushiRuntime
