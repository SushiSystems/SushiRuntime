/**************************************************************************/
/* host_export.hpp                                                        */
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
 * @file host_export.hpp
 * @brief Bundles a host-exported buffer with the address and size another API imports.
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/api/data/buffer.hpp>

namespace SushiRuntime
{
    namespace Interop
    {
        /**
         * @brief Holds a host-exported buffer alongside the address another API imports.
         *
         * See docs/design/SYCL_VULKAN_INTEROP.md §3.2 in the SushiEngine repository.
         *
         * @tparam T Element type of the exported buffer.
         */
        template <typename T>
        struct HostExport
        {
            API::Buffer<T> buffer;         /**< Owns the exported allocation. */
            void* address = nullptr;       /**< Holds the exported region's base address. */
            std::size_t bytes = 0;         /**< Holds the exported region's rounded byte size. */
        };

    } // namespace Interop
} // namespace SushiRuntime
