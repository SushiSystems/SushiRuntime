/**************************************************************************/
/* coherence_planner.hpp                                                  */
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
 * @file coherence_planner.hpp
 * @brief Declares CoherencePlanner, which plans the allocate, copy and free steps a graph needs.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <SushiRuntime/coherence/copy_plan.hpp>
#include <SushiRuntime/coherence/transfer_cost.hpp>
#include <SushiRuntime/coherence/transfer_path.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>
#include <SushiRuntime/memory/coherence_intervals.hpp>
#include <SushiRuntime/memory/memory_space.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds what one node of a graph touches and the space it runs on. */
        struct NodeAccess
        {
            bool planned = false;              /**< False for a node the planner leaves alone. */
            MemorySpaceID space;               /**< Space the node runs on. */
            std::vector<BufferRegion> reads;   /**< Byte ranges the node reads. */
            std::vector<BufferRegion> writes;  /**< Byte ranges the node writes. */
        };

        /**
         * @brief Plans the allocate, copy and free steps that keep every instance a graph's nodes
         *        read coherent.
         *
         * A plan is a pure function of the nodes, the directory's state, the spaces, the paths and
         * the cost: no clock, address or hash order decides it. A plan leaves every buffer valid on
         * the spaces it was valid on before, so a graph run again meets the state it was planned for.
         */
        class CoherencePlanner final
        {
            public:
                /**
                 * @brief Builds a planner over a directory, spaces, paths and a cost.
                 * @param spaces The spaces in DeviceKey order; the first of two equal sources wins.
                 * @param paths  The paths in order of preference; the first of two equal paths wins.
                 * @param cost   Prices one copy over one path; pure.
                 */
                CoherencePlanner(Memory::ResidencyDirectory& directory,
                                 std::vector<const Memory::MemorySpace*> spaces,
                                 std::vector<const TransferPath*> paths, TransferCost cost);

                /**
                 * @brief Plans the steps nodes need, in node order, and writes the start state of
                 *        each buffer it meets into the directory when the directory held none.
                 * @return The plan, or Unsupported when no path reaches a space a node reads on.
                 */
                [[nodiscard]] Result<CopyPlan> plan(const std::vector<NodeAccess>& nodes);

            private:
                /** @brief Holds what the planner tracks of one buffer while it plans. */
                struct BufferState
                {
                    std::size_t size = 0;              /**< Bytes of the buffer. */
                    std::size_t alignment = 1;         /**< Alignment of one element. */
                    std::uint64_t instances = 0;       /**< Bit i set when space i holds an instance. */
                    std::uint64_t allocated = 0;       /**< Instances this plan made. */
                    Memory::CoherenceIntervals start;  /**< The state before the graph. */
                    Memory::CoherenceIntervals current; /**< The state after the nodes planned so far. */
                };

                /** @brief Returns the state of handle, reading it from the directory once; null when skipped. */
                [[nodiscard]] BufferState* state_of(GlobalHandle handle);

                /** @brief Adds an allocation on space before node when the buffer has no instance there. */
                void ensure_instance(GlobalHandle handle, BufferState& state, MemorySpaceID space,
                                     std::size_t node, CopyPlan& plan) const;

                /** @brief Adds the copies that make [begin, end) valid on space, before node. */
                [[nodiscard]] Result<void> make_valid(GlobalHandle handle, BufferState& state,
                                                      std::size_t begin, std::size_t end,
                                                      MemorySpaceID space, std::size_t node,
                                                      CopyPlan& plan) const;

                /** @brief Adds the copies and frees that return every buffer to its start state. */
                [[nodiscard]] Result<void> restore(CopyPlan& plan);

                /** @brief Returns the space with id, or null when the planner has none. */
                [[nodiscard]] const Memory::MemorySpace* space_with(MemorySpaceID id) const noexcept;

                /// @brief Directory read for instances and start states.
                Memory::ResidencyDirectory* directory_;

                /// @brief Spaces in DeviceKey order.
                std::vector<const Memory::MemorySpace*> spaces_;

                /// @brief Paths in order of preference.
                std::vector<const TransferPath*> paths_;

                /// @brief Price of one copy over one path.
                TransferCost cost_;

                /// @brief State of each buffer met by the current plan, in handle order.
                std::map<GlobalHandle, BufferState> buffers_;
        };

        /** @brief Returns the byte range [begin, end) of region clipped to a buffer of size bytes. */
        [[nodiscard]] inline std::pair<std::size_t, std::size_t> byte_range(const BufferRegion& region,
                                                                            std::size_t size) noexcept
        {
            const std::size_t begin = region.offset < size ? region.offset : size;
            const std::size_t room = size - begin;
            const std::size_t length = region.length < room ? region.length : room;
            return {begin, begin + length};
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
