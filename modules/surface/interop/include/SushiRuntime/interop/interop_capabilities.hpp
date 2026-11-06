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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
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
#include <SushiRuntime/interop/external_handle_kind_set.hpp>

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
         * GPU, it reports the external-memory-import aspect, device_uuid holds a value and
         * importable_kinds holds at least one kind.
         */
        bool device_import = false;
        /**
         * @brief Holds the handle kinds import_device accepts on the device.
         *
         * Empty whenever device_import is false.
         */
        ExternalHandleKindSet importable_kinds;
        /** @brief Holds the device's UUID, when it has one. */
        std::optional<DeviceUUID> device_uuid;
    };
} // namespace SushiRuntime
