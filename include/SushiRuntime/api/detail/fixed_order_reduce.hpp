/**************************************************************************/
/* fixed_order_reduce.hpp                                                 */
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
 * @file fixed_order_reduce.hpp
 * @brief Declares the level plan, scratch and tile fold of the fixed-order reduction.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Fixes how many elements one work-item folds sequentially at every level.
             * @see include/SushiRuntime/api/detail/README.md
             */
            inline constexpr std::size_t REDUCE_TILE = 256;

            /**
             * @brief Describes one level of a fixed-tile tree reduction.
             * @see include/SushiRuntime/api/detail/README.md
             */
            struct ReduceLevel
            {
                std::size_t in_count = 0;   /**< Values this level folds. */
                std::size_t out_count = 0;  /**< Partials it produces. */
                std::size_t in_offset = 0;  /**< Element offset of its input in scratch. */
                std::size_t out_offset = 0; /**< Element offset of its output in scratch. */
            };

            /**
             * @brief Lays out the levels of a fixed-tile reduction over n values.
             * @return The levels in execution order; empty when n is zero.
             * @see include/SushiRuntime/api/detail/README.md
             */
            inline std::vector<ReduceLevel> plan_reduction(std::size_t n)
            {
                std::vector<ReduceLevel> levels;
                if (n == 0)
                    return levels;

                std::size_t count = n;
                std::size_t scratch_used = 0;
                std::size_t previous_out_offset = 0;

                while (true)
                {
                    ReduceLevel level;
                    level.in_count = count;
                    level.out_count = (count + REDUCE_TILE - 1) / REDUCE_TILE;
                    level.in_offset = previous_out_offset;
                    level.out_offset = scratch_used;

                    scratch_used += level.out_count;
                    previous_out_offset = level.out_offset;
                    count = level.out_count;

                    levels.push_back(level);
                    if (count == 1)
                        break;
                }
                return levels;
            }

            /**
             * @brief Returns the scratch element count a planned reduction needs.
             * @return Element count to allocate; zero when the reduction is one level.
             * @see include/SushiRuntime/api/detail/README.md
             */
            inline std::size_t reduce_scratch_count(const std::vector<ReduceLevel>& levels)
            {
                std::size_t total = 0;
                for (std::size_t i = 0; i + 1 < levels.size(); ++i)
                    total += levels[i].out_count;
                return total;
            }

            /**
             * @brief Allocates the USM scratch a reduction's intermediate levels use.
             * @param count Elements to allocate; must be non-zero.
             * @throws Core::SushiException If the allocation fails.
             * @see include/SushiRuntime/api/detail/README.md
             */
            template <typename T>
            inline std::shared_ptr<T> make_reduce_scratch(
                std::shared_ptr<Execution::RuntimeContext> ctx,
                std::size_t count, DeviceIndex device_index, Residency residency)
            {
                const std::size_t bytes = count * sizeof(T);
                void* raw = malloc_residency_on(*ctx, bytes, device_index, residency);

                if (raw == nullptr)
                    throw Core::SushiException(Core::Error{
                        Core::Errc::allocation_failed,
                        "Reduction scratch allocation of " + std::to_string(bytes) +
                        " bytes failed (out of USM memory)."});

                return std::shared_ptr<T>(static_cast<T*>(raw),
                                          [ctx](T* p) { ctx->memory().release(p); });
            }

            /**
             * @brief Folds one tile left to right, seeded with the tile's first element.
             * @param begin First index of the tile; must be < end.
             * @see include/SushiRuntime/api/detail/README.md
             */
            template <typename T, class Combine>
            inline T fold_tile(const Combine& op, const T* src,
                               std::size_t begin, std::size_t end)
            {
                T accumulator = src[begin];
                for (std::size_t j = begin + 1; j < end; ++j)
                    accumulator = op(accumulator, src[j]);
                return accumulator;
            }

        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
