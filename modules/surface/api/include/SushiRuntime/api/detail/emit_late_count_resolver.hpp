/**************************************************************************/
/* emit_late_count_resolver.hpp                                           */
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
 * @file emit_late_count_resolver.hpp
 * @brief Declares the description of the node that resolves late counts for a run.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/late_count_resolution.hpp>
#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/api/vocabulary/late_count.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Describes the single task that resolves K late counts.
         * @param fault The node's fault record; the step that follows reads it.
         * @param fill  Device callable run once as fill(live), in the order of counts.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <std::size_t K, class Fill>
        NodeDescription late_count_resolver_node(const std::array<LateCount, K>& counts,
                                                 std::uint32_t* fault, Fill fill,
                                                 std::vector<BufferRegion> reads,
                                                 std::vector<BufferRegion> writes,
                                                 std::size_t width)
        {
            NodeDescription description;
            description.reads = std::move(reads);
            description.writes = std::move(writes);
            description.width = width;
            description.work = CommandGroupWork([counts, fault, fill](sycl::handler& h)
            {
                std::array<LiveCountSource, K> sources;
                for (std::size_t k = 0; k < K; ++k)
                    sources[k] = late_count_source(counts[k], fault);
                h.single_task([sources, fault, fill]()
                {
                    std::array<std::size_t, K> live{};
                    for (std::size_t k = 0; k < K; ++k)
                        live[k] = read_live_count(sources[k], fault);
                    fill(live);
                });
            });
            return description;
        }
    } // namespace Detail
} // namespace SushiRuntime
