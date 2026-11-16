/**************************************************************************/
/* step_simulation.hpp                                                    */
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

/**
 * @file step_simulation.hpp
 * @brief Declares StepSimulation, the timeline of one modelled step as nodes are committed to devices.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/partitioning/partition_model.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Runs the nodes of a model one at a time on device timelines and prices their copies.
         *
         * A node starts after its predecessors, once its read bytes are valid in its pool, in the
         * earliest idle gap of its device that holds it. A range missing from the pool is copied once
         * from its writer's device; a second reader there waits for that copy and pays nothing.
         * @see modules/engine/partitioning/README.md
         */
        class SUSHIRUNTIME_API StepSimulation
        {
            public:
                /**
                 * @brief Starts an empty step over model, which must outlive the simulation.
                 *
                 * A planned handle starts valid on its home node's device once that node is committed;
                 * a read before then counts as local and makes the reader its source.
                 */
                explicit StepSimulation(const PartitionModel& model);

                /** @brief Starts an empty step over model whose every node's device placement already names. */
                StepSimulation(const PartitionModel& model, const std::vector<std::size_t>& placement);

                /**
                 * @brief Returns when node would finish on device, changing nothing.
                 * @pre Every predecessor of node is committed.
                 */
                [[nodiscard]] double finish_on(std::size_t node, std::size_t device) const;

                /**
                 * @brief Runs node on device: books its copies, its time slot and its writes.
                 * @pre Every predecessor of node is committed and node is not.
                 */
                void commit(std::size_t node, std::size_t device);

                /** @brief Returns when the committed node finished. */
                [[nodiscard]] double finish_of(std::size_t node) const
                {
                    return finish_[node];
                }

                /** @brief Returns the latest finish of every committed node, zero when there is none. */
                [[nodiscard]] double makespan() const noexcept
                {
                    return makespan_;
                }

                /** @brief Returns the bytes copied between pools so far. */
                [[nodiscard]] std::uint64_t copied_bytes() const noexcept
                {
                    return copied_bytes_;
                }

            private:
                /** @brief Holds one version of a byte range: the device that holds its source and when it is valid per pool. */
                struct Segment
                {
                    std::uint64_t end = 0;               /**< One past the last byte. */
                    std::optional<std::size_t> source;   /**< Device a copy reads it from; nothing before its first toucher. */
                    std::vector<double> ready;           /**< When it is valid in each pool; infinity where it is not. */
                };

                /** @brief Holds one busy slot of a device. */
                struct Slot
                {
                    double start = 0.0; /**< When the slot starts. */
                    double end = 0.0;   /**< When the slot ends. */
                };

                /** @brief Holds the bytes of one read and how they reach the reader. */
                struct Piece
                {
                    std::size_t handle = 0;  /**< Dense handle index. */
                    std::uint64_t begin = 0; /**< First byte. */
                    std::uint64_t end = 0;   /**< One past the last byte. */
                    bool fresh = false;      /**< Whether no node touched the bytes yet, so the reader becomes their home. */
                    std::optional<std::size_t> source; /**< Device the bytes are copied from; nothing when they are valid already. */
                    double ready = 0.0;      /**< When the bytes are valid at the source, or in the reader's pool. */
                };

                /** @brief Holds what a node's reads cost on one device. */
                struct ReadPlan
                {
                    std::vector<Piece> pieces;     /**< Pieces in handle and byte order, overlaps removed. */
                    std::vector<double> arrival;   /**< When each source device's copies land, per device index. */
                    std::vector<std::uint64_t> bytes; /**< Bytes copied from each source device. */
                    double ready = 0.0;            /**< When every byte is valid in the reader's pool. */
                };

                /** @brief Returns the segment a byte of handle holds before any node touched it. */
                [[nodiscard]] Segment initial_segment(std::size_t handle, std::uint64_t end) const;

                /** @brief Returns the pieces, copies and ready time of node's reads on device. */
                [[nodiscard]] ReadPlan plan_reads(std::size_t node, std::size_t device) const;

                /** @brief Returns the earliest start at or after ready of a gap of device that holds duration. */
                [[nodiscard]] double earliest_start(std::size_t device, double ready, double duration) const;

                /** @brief Returns when node may start for its edges alone. */
                [[nodiscard]] double predecessors_done(std::size_t node) const;

                /** @brief Makes [begin, end) of handle a run of whole segments, filling gaps with initial ones. */
                void materialize(std::size_t handle, std::uint64_t begin, std::uint64_t end);

                /** @brief Splits the segment of handle that holds position, so a segment starts there. */
                void split_at(std::size_t handle, std::uint64_t position);

                /// @brief The model the step runs.
                const PartitionModel* model_;

                /// @brief Segments of each handle, keyed by first byte; bytes outside them are untouched.
                std::vector<std::map<std::uint64_t, Segment>> segments_;

                /// @brief Busy slots of each device, by start.
                std::vector<std::vector<Slot>> timelines_;

                /// @brief Device of each node, known once committed or from the placement given up front.
                std::vector<std::optional<std::size_t>> device_of_;

                /// @brief Finish of each committed node.
                std::vector<double> finish_;

                /// @brief Latest finish so far.
                double makespan_ = 0.0;

                /// @brief Bytes copied between pools so far.
                std::uint64_t copied_bytes_ = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
