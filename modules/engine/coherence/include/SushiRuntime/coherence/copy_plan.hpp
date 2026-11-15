/**************************************************************************/
/* copy_plan.hpp                                                          */
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
 * @file copy_plan.hpp
 * @brief Declares CopyPlan, the allocate, copy and free steps the planner adds to a graph.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Names what one planned step does. */
        enum class PlannedStepKind : std::uint8_t
        {
            Allocate, /**< Makes the instance of a buffer on a space. */
            Copy,     /**< Copies a byte range of a buffer from one space to another. */
            Free      /**< Frees the instance of a buffer on a space. */
        };

        /** @brief Holds one step the planner adds to a graph. */
        struct PlannedStep
        {
            /** @brief Value of node for a step that runs after every node of the graph. */
            static constexpr std::size_t K_GRAPH_END = std::numeric_limits<std::size_t>::max();

            PlannedStepKind kind = PlannedStepKind::Copy; /**< What the step does. */
            GlobalHandle handle;                          /**< Buffer the step works on. */
            MemorySpaceID space;                          /**< Space made, written or freed. */
            MemorySpaceID source;                         /**< Space a copy reads; unused otherwise. */
            std::size_t begin = 0;                        /**< First byte of the range. */
            std::size_t end = 0;                          /**< One past the last byte of the range. */
            std::size_t alignment = 1;                    /**< Alignment an allocation asks for. */
            std::size_t path = 0;                         /**< Index of the path a copy takes. */
            std::size_t node = 0;                         /**< Node the step runs before, or K_GRAPH_END. */

            /** @brief Returns whether both steps are equal in every field. */
            [[nodiscard]] bool operator==(const PlannedStep& other) const noexcept
            {
                return kind == other.kind && handle == other.handle && space == other.space &&
                       source == other.source && begin == other.begin && end == other.end &&
                       alignment == other.alignment && path == other.path && node == other.node;
            }
        };

        /**
         * @brief Holds the steps of one plan in the order they are added.
         *
         * Steps before a node come in node order; steps at K_GRAPH_END come last, copies before
         * frees.
         */
        struct CopyPlan
        {
            std::vector<PlannedStep> steps; /**< The steps, in order. */
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
