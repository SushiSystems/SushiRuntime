/**************************************************************************/
/* emit_tracked.hpp                                                       */
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
 * @file emit_tracked.hpp
 * @brief Declares the replay callables of the launches ordered by the regions they name.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/graph/backing_graph.hpp>
#include <SushiRuntime/api/launch/extent.hpp>
#include <SushiRuntime/api/launch/nd_range.hpp>
#include <SushiRuntime/memory/resource_region.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Builds the replay of a per-element launch over [0, n).
             * @param width Launch width stamped on the node.
             */
            template <class F>
            auto per_element_node(std::vector<Core::ResourceRegion> reads,
                                  std::vector<Core::ResourceRegion> writes,
                                  std::size_t n, F&& fn, std::size_t width)
            {
                return [fn = std::forward<F>(fn), n, reads = std::move(reads),
                        writes = std::move(writes), width]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, n](sycl::handler& h)
                    {
                        h.parallel_for(sycl::range<1>(n),
                                       [fn](sycl::id<1> i) { fn(i[0]); });
                    }, reads, writes, {}, registration);
                    bg.width_last_node(width);
                };
            }

            /** @brief Builds the replay of a launch that drives the handler over [0, n) itself. */
            template <class F>
            auto range_node(std::vector<Core::ResourceRegion> reads,
                            std::vector<Core::ResourceRegion> writes,
                            std::size_t n, F&& fn, std::size_t width)
            {
                return [fn = std::forward<F>(fn), n, reads = std::move(reads),
                        writes = std::move(writes), width]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, n](sycl::handler& h)
                    {
                        fn(std::size_t{0}, n, h);
                    }, reads, writes, {}, registration);
                    bg.width_last_node(width);
                };
            }

            /** @brief Builds the replay of a raw command group. */
            template <class F>
            auto raw_node(std::vector<Core::ResourceRegion> reads,
                          std::vector<Core::ResourceRegion> writes, F&& fn, std::size_t width)
            {
                return [fn = std::forward<F>(fn), reads = std::move(reads),
                        writes = std::move(writes), width]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node(fn, reads, writes, {}, registration);
                    bg.width_last_node(width);
                };
            }

            /** @brief Builds the replay of a launch over an Extent; fn receives the shifted id. */
            template <int N, class F>
            auto extent_node(std::vector<Core::ResourceRegion> reads,
                             std::vector<Core::ResourceRegion> writes,
                             Extent<N> e, F&& fn, std::size_t width)
            {
                return [fn = std::forward<F>(fn), e, reads = std::move(reads),
                        writes = std::move(writes), width]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, e](sycl::handler& h)
                    {
                        const sycl::id<N> off = e.offset;
                        h.parallel_for(e.extent,
                                       [fn, off](sycl::id<N> idx) { fn(idx + off); });
                    }, reads, writes, {}, registration);
                    bg.width_last_node(width);
                };
            }

            /** @brief Builds the replay of a tiled launch over an NdRange. */
            template <int N, class F>
            auto nd_range_node(std::vector<Core::ResourceRegion> reads,
                               std::vector<Core::ResourceRegion> writes,
                               NdRange<N> r, F&& fn, std::size_t width)
            {
                return [fn = std::forward<F>(fn), r, reads = std::move(reads),
                        writes = std::move(writes), width]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, r](sycl::handler& h)
                    {
                        h.parallel_for(r.to_nd_range(),
                                       [fn](sycl::nd_item<N> it) { fn(it); });
                    }, reads, writes, {}, registration);
                    bg.width_last_node(width);
                };
            }
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
