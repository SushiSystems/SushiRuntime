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

#include <SushiRuntime/api/detail/emit_launch.hpp>
#include <SushiRuntime/api/graph/backing_graph.hpp>
#include <SushiRuntime/memory/resource_region.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Builds the replay of a launch ordered by the regions it names.
             * @tparam Launch std::size_t, Extent<N> or NdRange<N>.
             * @param width Launch width stamped on the node.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            template <class Launch, class F>
            auto launch_node(std::vector<Core::ResourceRegion> reads,
                             std::vector<Core::ResourceRegion> writes,
                             Launch launch, F&& fn, std::size_t width)
            {
                return [fn = std::forward<F>(fn), launch, reads = std::move(reads),
                        writes = std::move(writes), width]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, launch](sycl::handler& h)
                    {
                        submit_launch(h, launch, fn);
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
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
