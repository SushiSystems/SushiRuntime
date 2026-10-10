/**************************************************************************/
/* emit_captured.hpp                                                      */
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
 * @file emit_captured.hpp
 * @brief Declares the replay callable of an Extent launch that receives its data as pointers.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/callable_traits.hpp>
#include <SushiRuntime/api/graph/backing_graph.hpp>
#include <SushiRuntime/api/launch/extent.hpp>
#include <SushiRuntime/memory/resource_region.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Builds the replay of an Extent launch that hands its kernel raw pointers.
             * @tparam Pointers A std::tuple of raw pointers, handed to fn after the shifted id.
             */
            template <int N, class Pointers, class F>
            auto captured_node(std::vector<Core::ResourceRegion> reads,
                               std::vector<Core::ResourceRegion> writes,
                               Extent<N> e, Pointers ptrs, F&& fn, std::size_t width)
            {
                return [fn = std::forward<F>(fn), e, reads = std::move(reads),
                        writes = std::move(writes), ptrs, width]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, e, ptrs](sycl::handler& h)
                    {
                        const sycl::id<N> off = e.offset;
                        h.parallel_for(e.extent, [fn, off, ptrs](sycl::id<N> idx)
                        {
                            invoke_with_pointers(
                                fn, idx + off, ptrs,
                                std::make_index_sequence<std::tuple_size<Pointers>::value>{});
                        });
                    }, reads, writes, {}, registration);
                    bg.width_last_node(width);
                };
            }
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
