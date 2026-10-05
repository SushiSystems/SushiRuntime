/****************************************************************/
/* host_export.hpp                                              */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
