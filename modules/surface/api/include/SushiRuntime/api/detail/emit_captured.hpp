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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file emit_captured.hpp
 * @brief Declares the description of an Extent launch that receives its data as pointers.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/emit_launch.hpp>
#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/api/graph/access.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief Tells whether the last type of a pack is NodeOptions. */
        template <class... Ts>
        struct EndsInNodeOptions : std::false_type
        {
        };

        /** @brief Compares the one type left of a pack with NodeOptions. */
        template <class T>
        struct EndsInNodeOptions<T> : std::is_same<std::decay_t<T>, NodeOptions>
        {
        };

        /** @brief Drops the first type of a pack that holds more than one. */
        template <class T, class U, class... Ts>
        struct EndsInNodeOptions<T, U, Ts...> : EndsInNodeOptions<U, Ts...>
        {
        };

        /** @brief Tells whether the last type of a pack is NodeOptions. */
        template <class... Ts>
        inline constexpr bool ends_in_node_options_v = EndsInNodeOptions<Ts...>::value;

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
                    access.reads.push_back(BufferRegion(capture.key));
                if (Capture::writes)
                    access.writes.push_back(BufferRegion(capture.key));
                if (!capture.life.expired())
                    access.lives.push_back(capture.life);
            };
            (collect(captures), ...);
            return access;
        }

        /**
         * @brief Describes a launch that hands its kernel one pointer per capture.
         * @tparam Launch   std::size_t, Extent<N> or NdRange<N>.
         * @tparam Pointers A std::tuple of raw pointers, handed to kernel after the index.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class Launch, class Pointers, class F>
        NodeDescription captured_node(std::vector<BufferRegion> reads,
                                      std::vector<BufferRegion> writes,
                                      Launch launch, Pointers pointers, F&& kernel)
        {
            NodeDescription description;
            description.reads = std::move(reads);
            description.writes = std::move(writes);
            description.width = launch_count(launch);
            std::decay_t<F> body(std::forward<F>(kernel));
            if constexpr (has_host_form_v<Launch>)
                description.host_form = [body, launch, pointers]()
                {
                    std::apply([&body, &launch](auto... pointer)
                    {
                        run_launch_on_host(launch, body, pointer...);
                    }, pointers);
                };
            description.work = CommandGroupWork(
                [kernel = std::move(body), launch, pointers](sycl::handler& h)
                {
                    std::apply([&h, &kernel, &launch](auto... pointer)
                    {
                        submit_launch(h, launch, kernel, pointer...);
                    }, pointers);
                });
            return description;
        }
    } // namespace Detail
} // namespace SushiRuntime
