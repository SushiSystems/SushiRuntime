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
 * @brief Declares the description of a launch that reads its live width on the device.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/device_pass.hpp>
#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/api/graph/device_sized.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Describes a node run at capacity that reads its width on device.
         *
         * Work-items at or past *launch.live_width return at once; launch.live_words feeds
         * kernel one or two words by its arity (docs/design/GRAPH_REPLAY/README.md §D.2).
         * @pre The reads name the regions launch.live_width and launch.live_words lie in.
         */
        template <class F>
        NodeDescription device_sized_node(std::vector<BufferRegion> reads,
                                          std::vector<BufferRegion> writes,
                                          DeviceSized launch, F kernel, std::size_t width)
        {
            require_device_pass<F>();
            NodeDescription description;
            description.reads = std::move(reads);
            description.writes = std::move(writes);
            description.width = width;
            description.work = CommandGroupWork([kernel, launch](sycl::handler& h)
            {
                if (launch.capacity == 0)
                {
                    h.single_task([]() {});
                    return;
                }
                const std::uint32_t* live_count = launch.live_width;
                const std::uint32_t* live_scalar = launch.live_words;
                h.parallel_for(sycl::range<1>(launch.capacity),
                               [kernel, live_count, live_scalar](sycl::id<1> id)
                {
                    const std::size_t t = id[0];
                    if (live_count != nullptr && t >= static_cast<std::size_t>(*live_count))
                        return;
                    if constexpr (std::is_invocable_v<const F&, std::size_t,
                                                      std::size_t, std::size_t>)
                        kernel(t, static_cast<std::size_t>(live_scalar[0]),
                           static_cast<std::size_t>(live_scalar[1]));
                    else if constexpr (std::is_invocable_v<const F&, std::size_t, std::size_t>)
                        kernel(t, static_cast<std::size_t>(live_scalar[0]));
                    else
                    {
                        (void)live_scalar;
                        kernel(t);
                    }
                });
            });
            return description;
        }
    } // namespace Detail
} // namespace SushiRuntime
