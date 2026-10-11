/**************************************************************************/
/* host_export.hpp                                                        */
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
 * @file host_export.hpp
 * @brief Bundles a host-exported buffer with the address and size another API imports.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/interop/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/api/data/buffer.hpp>

namespace SushiRuntime
{
    /**
     * @brief Holds a host-exported buffer alongside the address another API imports.
     * @tparam T Element type of the exported buffer.
     * @see modules/surface/api/include/SushiRuntime/api/interop/README.md
     */
    template <typename T>
    struct HostExport
    {
        Buffer<T> buffer;        /**< Owns the exported allocation. */
        void* address = nullptr; /**< Holds the exported region's base address. */
        std::size_t bytes = 0;   /**< Holds the exported region's rounded byte size. */
    };
} // namespace SushiRuntime
