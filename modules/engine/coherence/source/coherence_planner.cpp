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

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <SushiRuntime/coherence/eviction_policy.hpp>
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

            /** @brief Returns whether map holds bytes that only the space named by bit holds. */
            bool holds_sole_copy(const Memory::CoherenceIntervals& map, std::uint64_t bit) noexcept
            {
                for (const Memory::CoherenceInterval& interval : map.intervals())
                    if (interval.valid_spaces == bit)
                        return true;
                return false;
            }

            /** @brief Returns whether kept holds, at every byte, every space that before held. */
            bool keeps_validity(const Memory::CoherenceIntervals& before,
                                const Memory::CoherenceIntervals& kept) noexcept
            {
                for (const Memory::CoherenceInterval& interval : before.intervals())
                    if ((interval.valid_spaces & ~kept.valid_spaces_at(interval.begin)) != 0)
                        return false;
                for (const Memory::CoherenceInterval& interval : kept.intervals())
                    if ((before.valid_spaces_at(interval.begin) & ~interval.valid_spaces) != 0)
                        return false;
                return true;
            }

            /** @brief Names what one piece of a node touches: the whole node, or one of its parts. */
            struct AccessPiece
            {
                MemorySpaceID space;                      /**< Space the piece runs on. */
                const std::vector<BufferRegion>* reads;   /**< Bytes it reads; not null. */
                const std::vector<BufferRegion>* writes;  /**< Bytes it writes; not null. */
            };

            /** @brief Returns the pieces access runs as: its parts in slice order, or the node whole. */
            [[nodiscard]] std::vector<AccessPiece> pieces_of(const NodeAccess& access)
            {
                std::vector<AccessPiece> pieces;
                if (access.parts.empty())
                {
                    pieces.push_back(AccessPiece{access.space, &access.reads, &access.writes});
                    return pieces;
                }
                pieces.reserve(access.parts.size());
                for (const NodeAccessPart& part : access.parts)
                    pieces.push_back(AccessPiece{part.space, &part.reads, &part.writes});
                return pieces;
            }
        } // namespace

        CoherencePlanner::CoherencePlanner(Memory::ResidencyView& directory,
                                           std::vector<const Memory::MemorySpace*> spaces,
                                           std::vector<const TransferPath*> paths, TransferCost cost)
            : directory_(&directory), spaces_(std::move(spaces)), paths_(std::move(paths)),
              cost_(std::move(cost))
        {

        }

        void CoherencePlanner::set_residency(ResidencyOptions options)
        {
            residency_ = std::move(options);
        }

        Result<CopyPlan> CoherencePlanner::plan(const std::vector<NodeAccess>& nodes)
        {
            buffers_.clear();
            uses_.clear();
            planned_bytes_.fill(0);
            node_count_ = nodes.size();
            CopyPlan plan;
            if (residency_.policy != nullptr)
                record_uses(nodes);
            for (std::size_t node = 0; node < nodes.size(); ++node)
            {
                if (!nodes[node].planned)
                    continue;
                // Parts run at once, so every part reads the bytes as they were before any part wrote.
                const std::vector<AccessPiece> pieces = pieces_of(nodes[node]);
                for (const AccessPiece& piece : pieces)
                {
                    Result<void> read = plan_reads(piece.space, *piece.reads, node, plan);
                    if (!read)
                        return make_error(std::move(read.error()));
                }
                for (const AccessPiece& piece : pieces)
                {
                    Result<void> written = plan_writes(piece.space, *piece.writes, node, plan);
                    if (!written)
                        return make_error(std::move(written.error()));
                }
            }

            Result<void> restored = residency_.policy != nullptr ? settle_resident(plan) : restore(plan);
            if (!restored)
                return make_error(std::move(restored.error()));
            return plan;
        }

        Result<void> CoherencePlanner::plan_reads(MemorySpaceID space, const std::vector<BufferRegion>& reads,
                                                  std::size_t node, CopyPlan& plan)
        {
            for (const BufferRegion& read : reads)
            {
                BufferState* const state = read.valid() ? state_of(read.base) : nullptr;
                if (state == nullptr)
                    continue;
                Result<void> ensured = ensure_instance(read.base, *state, space, node, plan);
                if (!ensured)
                    return make_error(std::move(ensured.error()));
                const std::pair<std::size_t, std::size_t> range = byte_range(read, state->size);
                Result<void> made = make_valid(read.base, *state, range.first, range.second, space, node, plan);
                if (!made)
                    return make_error(std::move(made.error()));
            }
            return {};
        }

        Result<void> CoherencePlanner::plan_writes(MemorySpaceID space, const std::vector<BufferRegion>& writes,
                                                   std::size_t node, CopyPlan& plan)
        {
            for (const BufferRegion& write : writes)
            {
                BufferState* const state = write.valid() ? state_of(write.base) : nullptr;
                if (state == nullptr)
                    continue;
                Result<void> ensured = ensure_instance(write.base, *state, space, node, plan);
                if (!ensured)
                    return make_error(std::move(ensured.error()));
                const std::pair<std::size_t, std::size_t> range = byte_range(write, state->size);
                state->current.record_write(range.first, range.second, space);
            }
            return {};
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
            if (entry.status != HandleStatus::Ok || entry.size == 0 || map == nullptr)
                return nullptr;
            // A buffer with no instance is planned only when its map covers its bytes, as a planned buffer's does.
            if (entry.valid_spaces == 0 && map->size() != entry.size)
                return nullptr;

            // A buffer no plan has met yet holds its bytes on its home space.
            if (map->size() != entry.size)
                map->reset(entry.size, entry.home);

            BufferState state;
            state.size = entry.size;
            state.alignment = entry.layout.alignment;
            state.instances = entry.valid_spaces;
            state.start_instances = entry.valid_spaces;
            state.home = entry.home;
            state.start = *map;
            state.current = *map;
            if (residency_.policy != nullptr)
                for (std::uint32_t space = 0; space < planned_bytes_.size(); ++space)
                    if ((state.instances & (std::uint64_t{1} << space)) != 0)
                        planned_bytes_[space] += state.size;
            return &buffers_.emplace(handle, std::move(state)).first->second;
        }

        void CoherencePlanner::record_uses(const std::vector<NodeAccess>& nodes)
        {
            for (std::size_t node = 0; node < nodes.size(); ++node)
            {
                if (!nodes[node].planned)
                    continue;
                for (const AccessPiece& piece : pieces_of(nodes[node]))
                    for (const std::vector<BufferRegion>* regions : {piece.reads, piece.writes})
                        for (const BufferRegion& region : *regions)
                        {
                            if (!region.valid() || state_of(region.base) == nullptr)
                                continue;
                            uses_[region.base].push_back(Use{node, piece.space.value()});
                        }
            }
        }

        Result<void> CoherencePlanner::ensure_instance(GlobalHandle handle, BufferState& state,
                                                       MemorySpaceID space, std::size_t node,
                                                       CopyPlan& plan)
        {
            const std::uint64_t wanted = bit_of(space);
            if ((state.instances & wanted) != 0)
                return {};

            if (residency_.policy != nullptr && wanted != 0)
            {
                Result<void> room = make_room(space, state.size, node, plan);
                if (!room)
                    return room;
                planned_bytes_[space.value()] += state.size;
            }

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
            return {};
        }

        Result<void> CoherencePlanner::make_room(MemorySpaceID space, std::size_t bytes,
                                                 std::size_t node, CopyPlan& plan)
        {
            const Memory::MemorySpace* const target = space_with(space);
            if (target == nullptr)
                return {};
            const std::size_t capacity = target->capacity_bytes();
            const std::size_t usable = capacity - std::min(target->reserve_bytes(), capacity);
            const std::uint64_t bit = bit_of(space);

            while (planned_bytes_[space.value()] + bytes > usable)
            {
                std::vector<EvictionCandidate> candidates;
                for (const auto& entry : buffers_)
                {
                    const BufferState& held = entry.second;
                    if ((held.instances & bit) == 0)
                        continue;
                    if (residency_.pins != nullptr && residency_.pins->is_pinned(entry.first, space))
                        continue;
                    if (used_at(entry.first, space.value(), node))
                        continue;
                    const bool sole = holds_sole_copy(held.current, bit);
                    // The host is where bytes are written back to; its only copy has nowhere to go.
                    if (sole && space == residency_.host)
                        continue;
                    EvictionCandidate candidate;
                    candidate.handle = entry.first;
                    candidate.space = space;
                    candidate.bytes = held.size;
                    candidate.next_read = next_use(entry.first, space.value(), node);
                    candidate.sole_copy = sole;
                    candidate.write_back_cost = sole ? write_back_cost(space, held.size) : 0.0;
                    candidates.push_back(candidate);
                }

                if (candidates.empty())
                    return make_error(Error{
                        ErrorCode::AllocationFailed,
                        "memory space " + std::to_string(space.value()) + " cannot hold " +
                            std::to_string(bytes) + " more bytes: " +
                            std::to_string(planned_bytes_[space.value()]) + " of " +
                            std::to_string(usable) + " are planned and nothing is evictable."});

                const std::optional<std::size_t> chosen = residency_.policy->choose(candidates);
                if (!chosen || *chosen >= candidates.size())
                    return make_error(Error{ErrorCode::InvalidState,
                                            "the eviction policy chose no victim among " +
                                                std::to_string(candidates.size()) + " candidates."});
                Result<void> evicted = evict(candidates[*chosen], node, plan);
                if (!evicted)
                    return evicted;
            }
            return {};
        }

        double CoherencePlanner::write_back_cost(MemorySpaceID space, std::size_t bytes) const
        {
            const Memory::MemorySpace* const source = space_with(space);
            const Memory::MemorySpace* const host = space_with(residency_.host);
            double least = std::numeric_limits<double>::infinity();
            if (source == nullptr || host == nullptr)
                return least;
            for (const TransferPath* path : paths_)
                if (path->connects(*source, *host))
                    least = std::min(least, cost_(*path, space, residency_.host, bytes));
            return least;
        }

        Result<void> CoherencePlanner::evict(const EvictionCandidate& victim, std::size_t node,
                                             CopyPlan& plan)
        {
            BufferState& state = buffers_.at(victim.handle);
            const std::uint64_t bit = bit_of(victim.space);
            const std::size_t first_step = plan.steps.size();

            if (victim.sole_copy)
            {
                const std::vector<Memory::CoherenceInterval> snapshot = state.current.intervals();
                for (const Memory::CoherenceInterval& interval : snapshot)
                {
                    if (interval.valid_spaces != bit)
                        continue;
                    Result<void> room = ensure_instance(victim.handle, state, residency_.host, node, plan);
                    if (!room)
                        return room;
                    Result<void> written = make_valid(victim.handle, state, interval.begin,
                                                      interval.end, residency_.host, node, plan);
                    if (!written)
                        return written;
                }
            }

            state.current.drop_space(victim.space);
            PlannedStep step;
            step.kind = PlannedStepKind::Free;
            step.handle = victim.handle;
            step.space = victim.space;
            step.end = state.size;
            step.node = node;
            plan.steps.push_back(step);
            state.instances &= ~bit;
            state.allocated &= ~bit;
            planned_bytes_[victim.space.value()] -= state.size;

            for (std::size_t index = first_step; index < plan.steps.size(); ++index)
                plan.steps[index].evicts = true;
            return {};
        }

        std::size_t CoherencePlanner::next_use(GlobalHandle handle, std::uint32_t space,
                                               std::size_t node) const
        {
            const auto found = uses_.find(handle);
            if (found == uses_.end())
                return EvictionCandidate::K_NEVER;
            std::size_t first = EvictionCandidate::K_NEVER;
            for (const Use& use : found->second)
            {
                if (use.space != space)
                    continue;
                if (use.node > node)
                    return use.node;
                if (first == EvictionCandidate::K_NEVER)
                    first = use.node;
            }
            // No later use in this graph: the next one is the first use of the next step.
            return first == EvictionCandidate::K_NEVER ? first : node_count_ + first;
        }

        bool CoherencePlanner::used_at(GlobalHandle handle, std::uint32_t space, std::size_t node) const
        {
            const auto found = uses_.find(handle);
            if (found == uses_.end())
                return false;
            return std::any_of(found->second.begin(), found->second.end(), [&](const Use& use)
            {
                return use.node == node && use.space == space;
            });
        }

        bool CoherencePlanner::is_steady() const
        {
            for (const auto& entry : buffers_)
            {
                const BufferState& state = entry.second;
                if ((state.start_instances & ~state.instances) != 0 ||
                    !keeps_validity(state.start, state.current))
                    return false;
            }
            return true;
        }

        Result<void> CoherencePlanner::settle_resident(CopyPlan& plan)
        {
            for (auto& entry : buffers_)
            {
                if (residency_.host_read.count(entry.first) == 0)
                    continue;
                BufferState& state = entry.second;
                Result<void> ensured = ensure_instance(entry.first, state, state.home,
                                                       PlannedStep::K_GRAPH_END, plan);
                if (!ensured)
                    return ensured;
                Result<void> made = make_valid(entry.first, state, 0, state.size, state.home,
                                               PlannedStep::K_GRAPH_END, plan);
                if (!made)
                    return made;
            }

            plan.steady = is_steady();
            for (auto& entry : buffers_)
                if (Memory::CoherenceIntervals* const map = directory_->coherence(entry.first))
                    *map = entry.second.current;
            return {};
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
