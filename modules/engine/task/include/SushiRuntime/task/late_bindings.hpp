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
    namespace Internal::Graph
    {
        /**
         * @brief Holds per-step execution controls re-read at dispatch.
         * @see modules/engine/graph/README.md
         */
        struct LateBindings
        {
            /** @brief count sentinel: iterate the capacity baked at compile time. */
            static constexpr std::size_t K_USE_BAKED = ~std::size_t{0};

            /** @brief When false the node is instant-complete: no kernel, successors fire at once. */
            std::atomic<bool> enabled{true};

            /** @brief Live iteration size for this step, or K_USE_BAKED for the full baked range. */
            std::atomic<std::size_t> count{K_USE_BAKED};

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
