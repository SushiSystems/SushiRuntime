/**************************************************************************/
/* emit_launch.hpp                                                        */
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
 * @file emit_launch.hpp
 * @brief Declares the launch traits and submit_launch, the one parallel_for of each launch.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <string>
#include <type_traits>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/launch/extent.hpp>
#include <SushiRuntime/api/launch/nd_range.hpp>
#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /** @brief Tells whether a type is an Extent. */
            template <class Launch>
            struct is_extent : std::false_type
            {

            };

            /** @brief Marks every rank of Extent. */
            template <int N>
            struct is_extent<Extent<N>> : std::true_type
            {

            };

            /** @brief Tells whether a type is an NdRange. */
            template <class Launch>
            struct is_nd_range : std::false_type
            {

            };

            /** @brief Marks every rank of NdRange. */
            template <int N>
            struct is_nd_range<NdRange<N>> : std::true_type
            {

            };

            /** @brief Tells whether a type is an element count: an integer other than bool. */
            template <class Launch>
            inline constexpr bool is_count_v =
                std::is_integral_v<Launch> && !std::is_same_v<Launch, bool>;

            /**
             * @brief Tells whether a type describes a launch: a count, an Extent or an NdRange.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            template <class Launch>
            inline constexpr bool is_launch_v =
                is_count_v<std::decay_t<Launch>> || is_extent<std::decay_t<Launch>>::value ||
                is_nd_range<std::decay_t<Launch>>::value;

            /** @brief Names the type a launch argument is recorded as: a count is a size_t. */
            template <class Launch>
            using launch_t = std::conditional_t<is_count_v<std::decay_t<Launch>>, std::size_t,
                                                std::decay_t<Launch>>;

            /** @brief Returns a launch argument as the type it is recorded as. */
            template <class Launch>
            constexpr launch_t<Launch> launch_of(const Launch& launch) noexcept
            {
                return static_cast<launch_t<Launch>>(launch);
            }

            /** @brief Returns how many work-items a count launches. */
            constexpr std::size_t launch_count(std::size_t count) noexcept
            {
                return count;
            }

            /** @brief Returns how many work-items an Extent launches. */
            template <int N>
            std::size_t launch_count(const Extent<N>& extent) noexcept
            {
                return extent.count();
            }

            /** @brief Returns how many work-items an NdRange launches. */
            template <int N>
            std::size_t launch_count(const NdRange<N>& range) noexcept
            {
                return range.count();
            }

            /** @brief Returns one past the highest index a count hands its kernel. */
            constexpr std::size_t launch_reach(std::size_t count) noexcept
            {
                return count;
            }

            /**
             * @brief Returns the element bound an Extent's kernel stays under.
             * @return Origin plus size for rank 1; the work-item count for a higher rank.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            template <int N>
            std::size_t launch_reach(const Extent<N>& extent) noexcept
            {
                return N == 1 ? extent.origin[0] + extent.extent[0] : extent.count();
            }

            /**
             * @brief Returns the element bound an NdRange's kernel stays under.
             * @return Origin plus size for rank 1; the work-item count for a higher rank.
             */
            template <int N>
            std::size_t launch_reach(const NdRange<N>& range) noexcept
            {
                return N == 1 ? range.origin[0] + range.global[0] : range.count();
            }

            /** @brief Writes a SYCL id or range as "{a, b, c}". */
            template <class Indices>
            std::string describe_indices(const Indices& indices, int rank)
            {
                std::string text = "{";
                for (int d = 0; d < rank; ++d)
                    text += (d == 0 ? "" : ", ") + std::to_string(indices[d]);
                return text + "}";
            }

            /** @brief Writes a count as the launch an error message names. */
            inline std::string describe_launch(std::size_t count)
            {
                return "the count " + std::to_string(count);
            }

            /** @brief Writes an Extent, with its origin, as the launch an error message names. */
            template <int N>
            std::string describe_launch(const Extent<N>& extent)
            {
                return "the extent " + describe_indices(extent.extent, N) + " at origin " +
                       describe_indices(extent.origin, N);
            }

            /** @brief Writes an NdRange, with its origin, as the launch an error message names. */
            template <int N>
            std::string describe_launch(const NdRange<N>& range)
            {
                return "the nd-range " + describe_indices(range.global, N) + " at origin " +
                       describe_indices(range.origin, N);
            }

            /**
             * @brief Throws when a launch reaches past the handle its kernel indexes.
             * @param size   Elements the handle named by holder has.
             * @param call   The call as the caller wrote it, which opens the message.
             * @throws Core::SushiException With invalid_graph, naming the rule and holder.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            template <class Launch>
            void require_launch_within(const Launch& launch, std::size_t size, const char* call,
                                       std::size_t rule, const char* holder)
            {
                const std::size_t reach = launch_reach(launch);
                if (reach <= size)
                    return;

                throw Core::SushiException(Core::Error{
                    Core::Errc::invalid_graph,
                    std::string(call) + ", rule " + std::to_string(rule) + ": " +
                        describe_launch(launch) + " reaches " + std::to_string(reach) +
                        " elements and " + holder + " holds " + std::to_string(size) +
                        "; shrink the launch or allocate the handle larger."});
            }

            /**
             * @brief Submits a count launch: fn(i, extra...) for each i in [0, count).
             * @param extra Values copied into the kernel and passed after the index.
             */
            template <class Fn, class... Extra>
            void submit_launch(sycl::handler& handler, std::size_t count, const Fn& fn,
                               Extra... extra)
            {
                handler.parallel_for(sycl::range<1>(count), [fn, extra...](sycl::id<1> index)
                {
                    fn(index[0], extra...);
                });
            }

            /**
             * @brief Submits an Extent launch: fn(id, extra...) with the origin added to id.
             * @param extra Values copied into the kernel and passed after the index.
             */
            template <int N, class Fn, class... Extra>
            void submit_launch(sycl::handler& handler, const Extent<N>& extent, const Fn& fn,
                               Extra... extra)
            {
                const sycl::id<N> origin = extent.origin;
                handler.parallel_for(extent.extent, [fn, origin, extra...](sycl::id<N> index)
                {
                    fn(index + origin, extra...);
                });
            }

            /**
             * @brief Submits an NdRange launch: fn(item, extra...) or fn(item, id, extra...).
             * @param extra Values copied into the kernel and passed after the index.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            template <int N, class Fn, class... Extra>
            void submit_launch(sycl::handler& handler, const NdRange<N>& range, const Fn& fn,
                               Extra... extra)
            {
                if constexpr (std::is_invocable_v<const Fn&, sycl::nd_item<N>, sycl::id<N>,
                                                  Extra...>)
                {
                    const sycl::id<N> origin = range.origin;
                    handler.parallel_for(range.to_nd_range(),
                                         [fn, origin, extra...](sycl::nd_item<N> item)
                    {
                        fn(item, item.get_global_id() + origin, extra...);
                    });
                }
                else
                {
                    handler.parallel_for(range.to_nd_range(),
                                         [fn, extra...](sycl::nd_item<N> item)
                    {
                        fn(item, extra...);
                    });
                }
            }
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
