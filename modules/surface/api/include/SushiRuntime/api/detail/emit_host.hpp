/**************************************************************************/
/* emit_host.hpp                                                          */
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
 * @file emit_host.hpp
 * @brief Declares the replay callable of a task that runs on a worker thread.
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

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief Builds the replay of a nullary host task ordered by the regions it names. */
        template <class F>
        auto host_node(std::vector<Internal::Memory::ResourceRegion> reads,
                       std::vector<Internal::Memory::ResourceRegion> writes, F&& fn, std::size_t width)
        {
            return [fn = std::forward<F>(fn), reads = std::move(reads),
                    writes = std::move(writes), width]
                   (::SushiRuntime::Internal::Graph::TaskGraph& bg,
                    std::optional<std::uint32_t> registration)
            {
                bg.add_native(reads, writes, fn, {}, registration).baked_width = width;
            };
        }
    } // namespace Detail
} // namespace SushiRuntime
