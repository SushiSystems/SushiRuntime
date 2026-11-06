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
 * @file emit_host.hpp
 * @brief Declares the description of a task that runs on a worker thread.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief Describes a nullary host task ordered by the regions it names. */
        template <class F>
        NodeDescription host_node(std::vector<BufferRegion> reads,
                                  std::vector<BufferRegion> writes, F&& kernel,
                                  std::size_t width)
        {
            NodeDescription description;
            description.kind = NodeKind::Host;
            description.reads = std::move(reads);
            description.writes = std::move(writes);
            description.width = width;
            description.work = WorkerWork(std::forward<F>(kernel));
            return description;
        }
    } // namespace Detail
} // namespace SushiRuntime
