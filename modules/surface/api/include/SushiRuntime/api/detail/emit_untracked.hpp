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

#include <SushiRuntime/api/detail/emit_launch.hpp>
#include <SushiRuntime/graph/task_graph.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Builds the replay of a launch that names no region and runs on one device.
         * @tparam Launch std::size_t, Extent<N> or NdRange<N>.
         * @param device Index of the device the node is placed on.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class Launch, class F>
        auto untracked_launch_node(Launch launch, std::size_t device, F&& kernel)
        {
            return [kernel = std::forward<F>(kernel), launch, device]
                   (::SushiRuntime::Internal::Graph::TaskGraph& backing_graph,
                    std::optional<std::uint32_t> registration)
            {
                auto& node = backing_graph.add_node([kernel, launch](sycl::handler& h)
                {
                    submit_launch(h, launch, kernel);
                }, std::vector<sycl::event>{}, registration);
                node.place_on(device);
                node.baked_width = launch_count(launch);
            };
        }

        /**
         * @brief Builds the replay of a region-less launch that drives the handler itself.
         * @param device Index of the device the node is placed on.
         */
        template <class F>
        auto untracked_range_node(std::size_t n, std::size_t device, F&& kernel)
        {
            return [kernel = std::forward<F>(kernel), n, device]
                   (::SushiRuntime::Internal::Graph::TaskGraph& backing_graph,
                    std::optional<std::uint32_t> registration)
            {
                auto& node = backing_graph.add_node([kernel, n](sycl::handler& h)
                {
                    kernel(std::size_t{0}, n, h);
                }, std::vector<sycl::event>{}, registration);
                node.place_on(device);
                node.baked_width = n;
            };
        }

        /**
         * @brief Builds the replay of a raw command group with no region.
         * @param device Index of the device the node is placed on.
         */
        template <class F>
        auto untracked_raw_node(std::size_t device, F&& kernel)
        {
            return [kernel = std::forward<F>(kernel), device]
                   (::SushiRuntime::Internal::Graph::TaskGraph& backing_graph,
                    std::optional<std::uint32_t> registration)
            {
                backing_graph.add_node(kernel, std::vector<sycl::event>{},
                                       registration).place_on(device);
            };
        }
    } // namespace Detail
} // namespace SushiRuntime
