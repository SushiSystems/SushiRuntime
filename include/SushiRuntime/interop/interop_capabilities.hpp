/**************************************************************************/
/* interop_capabilities.hpp                                               */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
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
