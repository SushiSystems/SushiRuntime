/**************************************************************************/
/* late_bindings.hpp                                                      */
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
 * @file late_bindings.hpp
 * @brief Declares LateBindings, the per-step controls re-read at dispatch.
 * @author Mustafa Garip
 * @see modules/engine/graph/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace SushiRuntime
{
    namespace Graph
    {
        /**
         * @brief Holds per-step execution controls re-read at dispatch.
         * @see modules/engine/graph/README.md
         */
        struct LateBindings
        {
            /** @brief count sentinel: iterate the capacity baked at compile time. */
            static constexpr std::size_t USE_BAKED = ~std::size_t{0};

            /** @brief When false the node is instant-complete: no kernel, successors fire at once. */
            std::atomic<bool> enabled{true};

            /** @brief Live iteration size for this step, or USE_BAKED for the full baked range. */
            std::atomic<std::size_t> count{USE_BAKED};

            /**
             * @brief Points at the host-readable location a predecessor writes the live count to.
             * @pre The allocation is Residency::Host.
             * @see modules/engine/graph/README.md
             */
            const std::uint32_t* device_count = nullptr;

            /** @brief Element index of the live count within device_count. */
            std::size_t device_count_index = 0;

            /**
             * @brief Holds the first element this step iterates; the kernel adds it to each index.
             * @see modules/engine/graph/README.md
             */
            std::atomic<std::size_t> base{0};

            /**
             * @brief Points at the host-readable location a predecessor writes the live base into.
             * @see modules/engine/graph/README.md
             */
            const std::uint32_t* device_base = nullptr;

            /** @brief Element index of the live base within device_base. */
            std::size_t device_base_index = 0;
        };

    } // namespace Graph
} // namespace SushiRuntime
