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
    namespace Interop
    {
        /**
         * @brief Reports what one device can offer a memory pairing.
         *
         * See docs/design/SYCL_VULKAN_INTEROP.md §3.2 in the SushiEngine repository.
         */
        struct InteropCapabilities
        {
            bool host_export = false;              /**< Reports whether the device can export host memory for another API to import. */
            bool device_import = false;             /**< Reports whether the device can import another API's exported device memory. */
            std::optional<DeviceUuid> device_uuid;  /**< Holds the device's UUID, or nothing when it lacks the aspect. */
        };

    } // namespace Interop
} // namespace SushiRuntime
