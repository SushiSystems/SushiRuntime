/**************************************************************************/
/* emit_task.hpp                                                          */
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
 * @file emit_task.hpp
 * @brief Declares the replay callable of a task that submits its own work on a queue.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/task_work.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Builds the replay of a task body ordered by the regions it names.
         * @param fn Callable invoked as fn(sycl::queue&, const std::vector<sycl::event>&);
         *        it returns the event that ends its work.
         * @param width Launch width stamped on the node.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class F>
        auto task_node(std::vector<Internal::Memory::ResourceRegion> reads,
                       std::vector<Internal::Memory::ResourceRegion> writes, F&& fn,
                       std::size_t width)
        {
            return [fn = std::forward<F>(fn), reads = std::move(reads),
                    writes = std::move(writes), width]
                   (::SushiRuntime::Internal::Graph::TaskGraph& bg,
                    std::optional<std::uint32_t> registration)
            {
                bg.add_task(::SushiRuntime::Internal::Graph::TaskMetadata{}, reads, writes,
                            ::SushiRuntime::Internal::Graph::HostWork(fn), {},
                            registration).baked_width = width;
            };
        }
    } // namespace Detail
} // namespace SushiRuntime
