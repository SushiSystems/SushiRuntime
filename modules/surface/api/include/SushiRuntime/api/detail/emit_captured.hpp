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
#include <type_traits>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/emit_launch.hpp>
#include <SushiRuntime/api/graph/access.hpp>
#include <SushiRuntime/api/graph/backing_graph.hpp>
#include <SushiRuntime/memory/resource_region.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Gathers the regions and the observers of In, Out and InOut captures.
             * @return What the captures read and write, in argument order.
             */
            template <class... Captures>
            Access captured_access(const Captures&... captures)
            {
                Access access;
                const auto collect = [&access](const auto& capture)
                {
                    using Capture = std::decay_t<decltype(capture)>;
                    if (Capture::reads)
                        access.reads.push_back(capture.key);
                    if (Capture::writes)
                        access.writes.push_back(capture.key);
                    if (!capture.life.expired())
                        access.lives.push_back(capture.life);
                };
                (collect(captures), ...);
                return access;
            }

            /**
             * @brief Builds the replay of a launch that hands its kernel one pointer per capture.
             * @tparam Launch   std::size_t, Extent<N> or NdRange<N>.
             * @tparam Pointers A std::tuple of raw pointers, handed to fn after the index.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            template <class Launch, class Pointers, class F>
            auto captured_node(std::vector<Core::ResourceRegion> reads,
                               std::vector<Core::ResourceRegion> writes,
                               Launch launch, Pointers pointers, F&& fn, std::size_t width)
            {
                return [fn = std::forward<F>(fn), launch, reads = std::move(reads),
                        writes = std::move(writes), pointers, width]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, launch, pointers](sycl::handler& h)
                    {
                        std::apply([&h, &fn, &launch](auto... pointer)
                        {
                            submit_launch(h, launch, fn, pointer...);
                        }, pointers);
                    }, reads, writes, {}, registration);
                    bg.width_last_node(width);
                };
            }
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
