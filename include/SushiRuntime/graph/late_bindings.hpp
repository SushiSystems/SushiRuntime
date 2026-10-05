/****************************************************************/
/* late_bindings.hpp                                            */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file late_bindings.hpp
 * @brief Declares LateBindings, the per-step controls re-read at dispatch.
 * @author Mustafa Garip
 * @see include/SushiRuntime/graph/README.md
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
         * @see include/SushiRuntime/graph/README.md
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
             * @see include/SushiRuntime/graph/README.md
             */
            const std::uint32_t* device_count = nullptr;

            /** @brief Element index of the live count within device_count. */
            std::size_t device_count_index = 0;

            /**
             * @brief Holds the first element this step iterates; the kernel adds it to each index.
             * @see include/SushiRuntime/graph/README.md
             */
            std::atomic<std::size_t> base{0};

            /**
             * @brief Points at the host-readable location a predecessor writes the live base into.
             * @see include/SushiRuntime/graph/README.md
             */
            const std::uint32_t* device_base = nullptr;

            /** @brief Element index of the live base within device_base. */
            std::size_t device_base_index = 0;
        };

    } // namespace Graph
} // namespace SushiRuntime
