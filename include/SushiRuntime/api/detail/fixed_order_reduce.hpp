/****************************************************************/
/* fixed_order_reduce.hpp                                       */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Elements one work-item folds sequentially at every level.
             *
             * The single number the whole determinism guarantee rests on: the tile
             * boundaries, and therefore which values meet which, are a function of
             * the element count and this constant alone. It is deliberately not
             * derived from the device's work-group size, the worker count, or
             * anything else that varies between runs or machines.
             */
            inline constexpr std::size_t REDUCE_TILE = 256;

            /**
             * @brief One level of a fixed-tile tree reduction.
             *
             * A level folds @c in_count values into @c out_count = ceil(in_count /
             * REDUCE_TILE) partials. Offsets are in elements, into the reduction's
             * scratch allocation; the first level reads the caller's input instead
             * of scratch and the last writes the caller's output instead of scratch,
             * so those two offsets are unused on those levels.
             */
            struct ReduceLevel
            {
                std::size_t in_count = 0;   /**< Values this level folds. */
                std::size_t out_count = 0;  /**< Partials it produces. */
                std::size_t in_offset = 0;  /**< Element offset of its input in scratch. */
                std::size_t out_offset = 0; /**< Element offset of its output in scratch. */
            };

            /**
             * @brief Lays out the levels of a fixed-tile reduction over @p n values.
             *
             * Repeatedly divides the count by REDUCE_TILE until one value remains,
             * assigning each level a disjoint scratch window so consecutive levels
             * never alias — which is what lets the ordinary region tracker order them
             * with no hand-written edges. Pure, so the layout can be asserted in a
             * test without a device.
             *
             * @param n Number of values to reduce; zero yields no levels.
             * @return The levels in execution order; empty when @p n is zero.
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
             * @brief Scratch elements a planned reduction needs.
             *
             * Every level but the last parks its partials in scratch; the last writes
             * the caller's output, so its window is never allocated. At REDUCE_TILE =
             * 256 this is under half a percent of the input.
             *
             * @param levels The plan from plan_reduction().
             * @return Element count to allocate; zero when the reduction is one level.
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
             *
             * Allocated on the same device and with the same residency as the data
             * being reduced, so the reduction never straddles a device context and a
             * device-resident field is not dragged into host-addressable memory. The
             * returned pointer owns the allocation through a deleter that keeps the
             * context alive, so scratch cannot outlive the runtime that owns it.
             *
             * @tparam T Element type of the reduction.
             * @param ctx          Runtime context that performs the allocation.
             * @param count        Elements to allocate; must be non-zero.
             * @param device_index Device queue to pin the allocation to.
             * @param residency    Residency of the input being reduced.
             * @return An owning pointer to @p count elements.
             * @throws Core::SushiException If the allocation fails.
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
                                          [ctx](T* p) { ctx->free_usm(p); });
            }

            /**
             * @brief Folds one tile left to right, starting from its first element.
             *
             * The whole ordering contract in one function: the accumulator is seeded
             * with the tile's first value and every later value is combined onto it in
             * increasing index order. Seeding from the first element rather than from
             * an identity keeps the identity out of the arithmetic entirely, so a
             * floating-point sum is exactly ((v0+v1)+v2)+… and nothing else.
             *
             * @tparam T       Element type.
             * @tparam Combine Binary combining functor.
             * @param op    The combiner.
             * @param src   Base pointer of the values.
             * @param begin First index of the tile; must be < @p end.
             * @param end   One past the tile's last index.
             * @return The folded value.
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
