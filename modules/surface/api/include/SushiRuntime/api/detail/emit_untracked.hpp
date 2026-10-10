/**************************************************************************/
/* emit_untracked.hpp                                                     */
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
 * @file emit_untracked.hpp
 * @brief Declares the replay callables of the launches that name no region.
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

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /** @brief Builds the replay of a per-element launch over [0, n) with no region. */
            template <class F>
            auto untracked_per_element_node(std::size_t n, F&& fn)
            {
                return [fn = std::forward<F>(fn), n]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, n](sycl::handler& h)
                    {
                        h.parallel_for(sycl::range<1>(n),
                                       [fn](sycl::id<1> idx) { fn(idx[0]); });
                    }, std::vector<sycl::event>{}, registration);
                };
            }

            /** @brief Builds the replay of a launch that drives the handler over [0, n) itself. */
            template <class F>
            auto untracked_range_node(std::size_t n, F&& fn)
            {
                return [fn = std::forward<F>(fn), n]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, n](sycl::handler& h)
                    {
                        fn(std::size_t{0}, n, h);
                    }, std::vector<sycl::event>{}, registration);
                };
            }

            /** @brief Builds the replay of a raw command group with no region. */
            template <class F>
            auto untracked_raw_node(F&& fn)
            {
                return [fn = std::forward<F>(fn)]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node(fn, std::vector<sycl::event>{}, registration);
                };
            }

            /** @brief Builds the replay of a launch over an Extent with no region. */
            template <int N, class F>
            auto untracked_extent_node(Extent<N> e, F&& fn)
            {
                return [fn = std::forward<F>(fn), e]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, e](sycl::handler& h)
                    {
                        const sycl::id<N> off = e.offset;
                        h.parallel_for(e.extent,
                                       [fn, off](sycl::id<N> idx) { fn(idx + off); });
                    }, std::vector<sycl::event>{}, registration);
                };
            }

            /** @brief Builds the replay of a tiled launch over an NdRange with no region. */
            template <int N, class F>
            auto untracked_nd_range_node(NdRange<N> r, F&& fn)
            {
                return [fn = std::forward<F>(fn), r]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, r](sycl::handler& h)
                    {
                        h.parallel_for(r.to_nd_range(),
                                       [fn](sycl::nd_item<N> it) { fn(it); });
                    }, std::vector<sycl::event>{}, registration);
                };
            }
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
