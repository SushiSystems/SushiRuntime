/****************************************************************/
/* interop_capabilities.hpp                                     */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
