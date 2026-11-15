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

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <SushiRuntime/coherence/copy_plan.hpp>
#include <SushiRuntime/coherence/residency_options.hpp>
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
         * @brief Plans the allocate, copy and free steps that keep a graph's reads coherent.
         *
         * A plan is a pure function of the nodes, the directory's state, the spaces, the paths and
         * the cost: no clock, address or hash order decides it. A plan leaves every buffer valid on
         * the spaces it was valid on before, so a graph run again meets the state it was planned for.
         * With ResidencyOptions it also evicts when a space is full; see the module README.
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

                /** @brief Turns capacity tracking, eviction and residency on; replaces earlier options. */
                void set_residency(ResidencyOptions options);

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
                    std::uint64_t start_instances = 0; /**< Instances the buffer had before the graph. */
                    MemorySpaceID home;                /**< Space the buffer was made on. */
                    Memory::CoherenceIntervals start;  /**< The state before the graph. */
                    Memory::CoherenceIntervals current; /**< The state after the nodes planned so far. */
                };

                /** @brief Holds one place a buffer is used: a node and the space it runs on. */
                struct Use
                {
                    std::size_t node = 0;      /**< Position of the node in the graph. */
                    std::uint32_t space = 0;   /**< Space the node runs on. */
                };

                /** @brief Returns the state of handle, reading it from the directory once; null when skipped. */
                [[nodiscard]] BufferState* state_of(GlobalHandle handle);

                /**
                 * @brief Adds an allocation on space before node when the buffer has no instance
                 *        there, after making room for it when the space is full.
                 */
                [[nodiscard]] Result<void> ensure_instance(GlobalHandle handle, BufferState& state,
                                                           MemorySpaceID space, std::size_t node,
                                                           CopyPlan& plan);

                /** @brief Records the nodes that use each buffer; runs only with residency options. */
                void record_uses(const std::vector<NodeAccess>& nodes);

                /** @brief Evicts instances from space until bytes more fit, through the policy. */
                [[nodiscard]] Result<void> make_room(MemorySpaceID space, std::size_t bytes,
                                                     std::size_t node, CopyPlan& plan);

                /** @brief Writes back, drops and frees one instance, before node. */
                [[nodiscard]] Result<void> evict(const EvictionCandidate& victim, std::size_t node,
                                                 CopyPlan& plan);

                /** @brief Returns the position of the next node that uses handle on space after node. */
                [[nodiscard]] std::size_t next_use(GlobalHandle handle, std::uint32_t space,
                                                   std::size_t node) const;

                /** @brief Returns whether the node at position uses handle on space. */
                [[nodiscard]] bool used_at(GlobalHandle handle, std::uint32_t space,
                                           std::size_t node) const;

                /** @brief Returns whether the end state of every buffer equals its start state. */
                [[nodiscard]] bool is_steady() const;

                /** @brief Adds the copies that make [begin, end) valid on space, before node. */
                [[nodiscard]] Result<void> make_valid(GlobalHandle handle, BufferState& state,
                                                      std::size_t begin, std::size_t end,
                                                      MemorySpaceID space, std::size_t node,
                                                      CopyPlan& plan) const;

                /** @brief Adds the copies and frees that return every buffer to its start state. */
                [[nodiscard]] Result<void> restore(CopyPlan& plan);

                /** @brief Copies the buffers the host reads back to their home and keeps the rest resident. */
                [[nodiscard]] Result<void> settle_resident(CopyPlan& plan);

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

                /// @brief Capacity rules; inactive while the policy is null.
                ResidencyOptions residency_;

                /// @brief Bytes planned on each space by the current plan.
                std::array<std::uint64_t, Memory::CoherenceIntervals::K_MAX_MEMORY_SPACES> planned_bytes_{};

                /// @brief Uses of each buffer in node order; filled only with residency options.
                std::map<GlobalHandle, std::vector<Use>> uses_;

                /// @brief Number of nodes of the current plan.
                std::size_t node_count_ = 0;

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
