/**************************************************************************/
/* emit_device_sized.hpp                                                  */
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
 * @file emit_device_sized.hpp
 * @brief Declares the replay callable of a launch that reads its live width on the device.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/graph/device_sized.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/memory/resource_region.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Builds the replay of a node run at capacity that reads its width on device.
         *
         * Work-items at or past *launch.live_width return at once; launch.live_words feeds
         * fn one or two words by its arity (docs/design/GRAPH_REPLAY.md §D.2).
         * @pre The reads name the regions launch.live_width and launch.live_words lie in.
         */
        template <class F>
        auto device_sized_node(std::vector<Internal::Memory::ResourceRegion> reads,
                               std::vector<Internal::Memory::ResourceRegion> writes,
                               DeviceSized launch, F fn, std::size_t width)
        {
            return [fn, launch, reads = std::move(reads), writes = std::move(writes), width]
                   (::SushiRuntime::Internal::Graph::TaskGraph& bg,
                    std::optional<std::uint32_t> registration)
            {
                bg.add_node([fn, launch](sycl::handler& h)
                {
                    if (launch.capacity == 0)
                    {
                        h.single_task([]() {});
                        return;
                    }
                    const std::uint32_t* live_count = launch.live_width;
                    const std::uint32_t* live_scalar = launch.live_words;
                    h.parallel_for(sycl::range<1>(launch.capacity),
                                   [fn, live_count, live_scalar](sycl::id<1> id)
                    {
                        const std::size_t t = id[0];
                        if (live_count != nullptr && t >= static_cast<std::size_t>(*live_count))
                            return;
                        if constexpr (std::is_invocable_v<const F&, std::size_t,
                                                          std::size_t, std::size_t>)
                            fn(t, static_cast<std::size_t>(live_scalar[0]),
                               static_cast<std::size_t>(live_scalar[1]));
                        else if constexpr (std::is_invocable_v<const F&, std::size_t, std::size_t>)
                            fn(t, static_cast<std::size_t>(live_scalar[0]));
                        else
                        {
                            (void)live_scalar;
                            fn(t);
                        }
                    });
                }, reads, writes, {}, registration).baked_width = width;
            };
        }
    } // namespace Detail
} // namespace SushiRuntime
