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
 * @brief Declares the descriptions of the launches ordered by the regions they name.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/emit_launch.hpp>
#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Describes a launch ordered by the regions it names.
         * @tparam Launch std::size_t, Extent<N> or NdRange<N>.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class Launch, class F>
        NodeDescription launch_node(std::vector<BufferRegion> reads,
                                    std::vector<BufferRegion> writes,
                                    Launch launch, F&& kernel)
        {
            NodeDescription description;
            description.reads = std::move(reads);
            description.writes = std::move(writes);
            description.width = launch_count(launch);
            description.work = CommandGroupWork(
                [kernel = std::forward<F>(kernel), launch](sycl::handler& h)
                {
                    submit_launch(h, launch, kernel);
                });
            return description;
        }

        /** @brief Describes a launch that drives the handler over [0, n) itself. */
        template <class F>
        NodeDescription range_node(std::vector<BufferRegion> reads,
                                   std::vector<BufferRegion> writes,
                                   std::size_t n, F&& kernel)
        {
            NodeDescription description;
            description.reads = std::move(reads);
            description.writes = std::move(writes);
            description.width = n;
            description.work = CommandGroupWork(
                [kernel = std::forward<F>(kernel), n](sycl::handler& h)
                {
                    kernel(std::size_t{0}, n, h);
                });
            return description;
        }

        /**
         * @brief Describes a raw command group.
         * @param width Launch width the caller stated, or K_WIDTH_UNKNOWN.
         */
        template <class F>
        NodeDescription raw_node(std::vector<BufferRegion> reads,
                                 std::vector<BufferRegion> writes, F&& kernel,
                                 std::size_t width)
        {
            NodeDescription description;
            description.reads = std::move(reads);
            description.writes = std::move(writes);
            description.width = width;
            description.work = CommandGroupWork(std::forward<F>(kernel));
            return description;
        }
    } // namespace Detail
} // namespace SushiRuntime
