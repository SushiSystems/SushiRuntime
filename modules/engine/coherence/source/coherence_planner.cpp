/**************************************************************************/
/* coherence_planner.cpp                                                  */
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
 * @file coherence_planner.cpp
 * @brief Implements CoherencePlanner.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/coherence_planner.hpp>

#include <optional>
#include <string>
#include <utility>

#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the bitmask naming space alone, or 0 past the last space a mask names. */
            std::uint64_t bit_of(MemorySpaceID space) noexcept
            {
                return space.value() < Memory::CoherenceIntervals::K_MAX_MEMORY_SPACES
                           ? std::uint64_t{1} << space.value()
                           : 0;
            }

            /** @brief Holds one byte range and the spaces that hold it. */
            struct Piece
            {
                std::size_t begin = 0;          /**< First byte. */
                std::size_t end = 0;            /**< One past the last byte. */
                std::uint64_t valid_spaces = 0; /**< Spaces that hold the current bytes. */
            };

            /** @brief Returns the parts of [begin, end) that space does not hold, in byte order. */
            std::vector<Piece> missing_pieces(const Memory::CoherenceIntervals& map, std::size_t begin,
                                              std::size_t end, MemorySpaceID space)
            {
                std::vector<Piece> pieces;
                const std::uint64_t wanted = bit_of(space);
                for (const Memory::CoherenceInterval& interval : map.intervals())
                {
                    const std::size_t from = interval.begin > begin ? interval.begin : begin;
                    const std::size_t to = interval.end < end ? interval.end : end;
                    if (from >= to || (interval.valid_spaces & wanted) != 0)
                        continue;
                    pieces.push_back(Piece{from, to, interval.valid_spaces});
                }
                return pieces;
            }

            /** @brief Returns whether handle names memory the planner never moves. */
            bool left_alone(GlobalHandle handle) noexcept
            {
                return has_flags(handle.flags(), HandleFlags::Borrowed) ||
                       has_flags(handle.flags(), HandleFlags::HostOnly);
            }
        } // namespace

        CoherencePlanner::CoherencePlanner(Memory::ResidencyDirectory& directory,
                                           std::vector<const Memory::MemorySpace*> spaces,
                                           std::vector<const TransferPath*> paths, TransferCost cost)
            : directory_(&directory), spaces_(std::move(spaces)), paths_(std::move(paths)),
              cost_(std::move(cost))
        {

        }

        Result<CopyPlan> CoherencePlanner::plan(const std::vector<NodeAccess>& nodes)
        {
            buffers_.clear();
            CopyPlan plan;
            for (std::size_t node = 0; node < nodes.size(); ++node)
            {
                const NodeAccess& access = nodes[node];
                if (!access.planned)
                    continue;

                for (const BufferRegion& read : access.reads)
                {
                    BufferState* const state = read.valid() ? state_of(read.base) : nullptr;
                    if (state == nullptr)
                        continue;
                    ensure_instance(read.base, *state, access.space, node, plan);
                    const std::pair<std::size_t, std::size_t> range = byte_range(read, state->size);
                    Result<void> made = make_valid(read.base, *state, range.first, range.second,
                                                   access.space, node, plan);
                    if (!made)
                        return make_error(std::move(made.error()));
                }

                for (const BufferRegion& write : access.writes)
                {
                    BufferState* const state = write.valid() ? state_of(write.base) : nullptr;
                    if (state == nullptr)
                        continue;
                    ensure_instance(write.base, *state, access.space, node, plan);
                    const std::pair<std::size_t, std::size_t> range = byte_range(write, state->size);
                    state->current.record_write(range.first, range.second, access.space);
                }
            }

            Result<void> restored = restore(plan);
            if (!restored)
                return make_error(std::move(restored.error()));
            return plan;
        }

        CoherencePlanner::BufferState* CoherencePlanner::state_of(GlobalHandle handle)
        {
            const auto found = buffers_.find(handle);
            if (found != buffers_.end())
                return &found->second;
            if (left_alone(handle))
                return nullptr;

            const Memory::EntryView entry = directory_->inspect(handle);
            Memory::CoherenceIntervals* const map = directory_->coherence(handle);
            if (entry.status != HandleStatus::Ok || entry.size == 0 || entry.valid_spaces == 0 ||
                map == nullptr)
                return nullptr;

            // A buffer no plan has met yet holds its bytes on its home space.
            if (map->size() != entry.size)
                map->reset(entry.size, entry.home);

            BufferState state;
            state.size = entry.size;
            state.alignment = entry.layout.alignment;
            state.instances = entry.valid_spaces;
            state.start = *map;
            state.current = *map;
            return &buffers_.emplace(handle, std::move(state)).first->second;
        }

        void CoherencePlanner::ensure_instance(GlobalHandle handle, BufferState& state,
                                               MemorySpaceID space, std::size_t node,
                                               CopyPlan& plan) const
        {
            const std::uint64_t wanted = bit_of(space);
            if ((state.instances & wanted) != 0)
                return;

            PlannedStep step;
            step.kind = PlannedStepKind::Allocate;
            step.handle = handle;
            step.space = space;
            step.end = state.size;
            step.alignment = state.alignment;
            step.node = node;
            plan.steps.push_back(step);
            state.instances |= wanted;
            state.allocated |= wanted;
        }

        Result<void> CoherencePlanner::make_valid(GlobalHandle handle, BufferState& state,
                                                  std::size_t begin, std::size_t end,
                                                  MemorySpaceID space, std::size_t node,
                                                  CopyPlan& plan) const
        {
            const Memory::MemorySpace* const destination = space_with(space);
            for (const Piece& piece : missing_pieces(state.current, begin, end, space))
            {
                // Bytes no space holds were never written; there is nothing to copy.
                if (piece.valid_spaces == 0)
                    continue;

                std::optional<PlannedStep> best;
                double best_cost = 0.0;
                for (const Memory::MemorySpace* source : spaces_)
                {
                    if ((piece.valid_spaces & bit_of(source->id())) == 0 || destination == nullptr)
                        continue;
                    for (std::size_t path = 0; path < paths_.size(); ++path)
                    {
                        if (!paths_[path]->connects(*source, *destination))
                            continue;
                        const double cost = cost_(*paths_[path], source->id(), space,
                                                  piece.end - piece.begin);
                        // Strictly cheaper only: the earlier source and path win a tie.
                        if (best && !(cost < best_cost))
                            continue;
                        PlannedStep step;
                        step.kind = PlannedStepKind::Copy;
                        step.handle = handle;
                        step.space = space;
                        step.source = source->id();
                        step.begin = piece.begin;
                        step.end = piece.end;
                        step.path = path;
                        step.node = node;
                        best = step;
                        best_cost = cost;
                    }
                }

                if (!best)
                    return make_error(Error{
                        ErrorCode::Unsupported,
                        "no transfer path reaches memory space " + std::to_string(space.value()) +
                            " from a space that holds the bytes; the node cannot read them there."});
                plan.steps.push_back(*best);
                state.current.record_copy(piece.begin, piece.end, space);
            }
            return {};
        }

        Result<void> CoherencePlanner::restore(CopyPlan& plan)
        {
            for (auto& entry : buffers_)
            {
                BufferState& state = entry.second;
                const std::vector<Memory::CoherenceInterval> start = state.start.intervals();
                for (const Memory::CoherenceInterval& interval : start)
                {
                    for (std::uint32_t space = 0; space < Memory::CoherenceIntervals::K_MAX_MEMORY_SPACES;
                         ++space)
                    {
                        if ((interval.valid_spaces & (std::uint64_t{1} << space)) == 0)
                            continue;
                        Result<void> made = make_valid(entry.first, state, interval.begin, interval.end,
                                                       MemorySpaceID(space), PlannedStep::K_GRAPH_END,
                                                       plan);
                        if (!made)
                            return make_error(std::move(made.error()));
                    }
                }
            }

            for (const auto& entry : buffers_)
            {
                for (std::uint32_t space = 0; space < Memory::CoherenceIntervals::K_MAX_MEMORY_SPACES;
                     ++space)
                {
                    if ((entry.second.allocated & (std::uint64_t{1} << space)) == 0)
                        continue;
                    PlannedStep step;
                    step.kind = PlannedStepKind::Free;
                    step.handle = entry.first;
                    step.space = MemorySpaceID(space);
                    step.end = entry.second.size;
                    step.node = PlannedStep::K_GRAPH_END;
                    plan.steps.push_back(step);
                }
            }
            return {};
        }

        const Memory::MemorySpace* CoherencePlanner::space_with(MemorySpaceID id) const noexcept
        {
            for (const Memory::MemorySpace* space : spaces_)
                if (space->id() == id)
                    return space;
            return nullptr;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
