/**************************************************************************/
/* step_simulation.cpp                                                    */
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
 * @file step_simulation.cpp
 * @brief Implements StepSimulation.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partitioning/step_simulation.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /// @brief The time of a version that is not valid in a pool.
            constexpr double K_NOT_VALID = std::numeric_limits<double>::infinity();
        } // namespace

        StepSimulation::StepSimulation(const PartitionModel& model)
            : model_(&model),
              segments_(model.handle_count()),
              timelines_(model.device_count()),
              device_of_(model.node_count()),
              finish_(model.node_count(), 0.0)
        {

        }

        StepSimulation::StepSimulation(const PartitionModel& model, const std::vector<std::size_t>& placement)
            : StepSimulation(model)
        {
            for (std::size_t node = 0; node < placement.size() && node < device_of_.size(); ++node)
                device_of_[node] = placement[node];
        }

        double StepSimulation::finish_on(std::size_t node, std::size_t device) const
        {
            const ReadPlan plan = plan_reads(node, device);
            const double ready = std::max(plan.ready, predecessors_done(node));
            const double duration = model_->node_time(node, device);
            return earliest_start(device, ready, duration) + duration;
        }

        void StepSimulation::commit(std::size_t node, std::size_t device)
        {
            const ReadPlan plan = plan_reads(node, device);
            const std::size_t pool = model_->pool_of(device);
            const double ready = std::max(plan.ready, predecessors_done(node));
            const double duration = model_->node_time(node, device);
            const double start = earliest_start(device, ready, duration);
            const double finish = start + duration;

            std::vector<Slot>& timeline = timelines_[device];
            const auto after = std::upper_bound(timeline.begin(), timeline.end(), start,
                                                [](double value, const Slot& slot) { return value < slot.start; });
            timeline.insert(after, Slot{start, finish});

            for (const Piece& piece : plan.pieces)
            {
                if (!piece.fresh && !piece.source)
                    continue;
                materialize(piece.handle, piece.begin, piece.end);
                std::map<std::uint64_t, Segment>& segments = segments_[piece.handle];
                for (auto it = segments.lower_bound(piece.begin); it != segments.end() && it->first < piece.end; ++it)
                {
                    if (piece.fresh)
                    {
                        it->second.source = device;
                        it->second.ready[pool] = 0.0;
                    }
                    else
                    {
                        it->second.ready[pool] = std::min(it->second.ready[pool], plan.arrival[*piece.source]);
                    }
                }
            }
            for (const std::uint64_t bytes : plan.bytes)
                copied_bytes_ += bytes;

            for (const PartitionModel::Range& range : model_->writes_of(node))
            {
                if (range.begin >= range.end)
                    continue;
                split_at(range.handle, range.begin);
                split_at(range.handle, range.end);
                std::map<std::uint64_t, Segment>& segments = segments_[range.handle];
                segments.erase(segments.lower_bound(range.begin), segments.lower_bound(range.end));
                Segment written{range.end, device, std::vector<double>(model_->pool_count(), K_NOT_VALID)};
                written.ready[pool] = finish;
                segments.emplace(range.begin, std::move(written));
            }

            device_of_[node] = device;
            finish_[node] = finish;
            makespan_ = std::max(makespan_, finish);
        }

        StepSimulation::Segment StepSimulation::initial_segment(std::size_t handle, std::uint64_t end) const
        {
            Segment segment{end, model_->stated_device(handle), std::vector<double>(model_->pool_count(), K_NOT_VALID)};
            const std::optional<std::size_t> home = model_->home_node(handle);
            if (!segment.source && home)
                segment.source = device_of_[*home];
            if (segment.source)
                segment.ready[model_->pool_of(*segment.source)] = 0.0;
            return segment;
        }

        StepSimulation::ReadPlan StepSimulation::plan_reads(std::size_t node, std::size_t device) const
        {
            const std::size_t pool = model_->pool_of(device);
            ReadPlan plan;
            plan.arrival.assign(model_->device_count(), 0.0);
            plan.bytes.assign(model_->device_count(), 0);

            const auto add_piece = [&](std::size_t handle, std::uint64_t begin, std::uint64_t end, const Segment& segment)
            {
                Piece piece{handle, begin, end, !segment.source, std::nullopt, 0.0};
                if (segment.source)
                {
                    if (std::isfinite(segment.ready[pool]))
                    {
                        piece.ready = segment.ready[pool];
                    }
                    else
                    {
                        piece.source = segment.source;
                        piece.ready = segment.ready[model_->pool_of(*segment.source)];
                    }
                }
                plan.pieces.push_back(piece);
            };

            for (const PartitionModel::Range& range : model_->reads_of(node))
            {
                const std::map<std::uint64_t, Segment>& segments = segments_[range.handle];
                std::uint64_t position = range.begin;
                auto it = segments.upper_bound(position);
                if (it != segments.begin() && std::prev(it)->second.end > position)
                    it = std::prev(it);
                while (position < range.end)
                {
                    if (it == segments.end() || it->first > position)
                    {
                        const std::uint64_t gap_end = it == segments.end() ? range.end : std::min(range.end, it->first);
                        add_piece(range.handle, position, gap_end, initial_segment(range.handle, gap_end));
                        position = gap_end;
                        continue;
                    }
                    const std::uint64_t piece_end = std::min(range.end, it->second.end);
                    add_piece(range.handle, position, piece_end, it->second);
                    position = piece_end;
                    ++it;
                }
            }

            // Pieces of one handle never overlap once each starts where the one before it ended.
            std::stable_sort(plan.pieces.begin(), plan.pieces.end(), [](const Piece& left, const Piece& right)
                             { return left.handle != right.handle ? left.handle < right.handle : left.begin < right.begin; });
            std::vector<Piece> disjoint;
            disjoint.reserve(plan.pieces.size());
            for (Piece piece : plan.pieces)
            {
                if (!disjoint.empty() && disjoint.back().handle == piece.handle)
                    piece.begin = std::max(piece.begin, disjoint.back().end);
                if (piece.begin >= piece.end)
                    continue;
                disjoint.push_back(piece);
            }
            plan.pieces = std::move(disjoint);

            for (const Piece& piece : plan.pieces)
            {
                if (piece.source)
                {
                    plan.bytes[*piece.source] += piece.end - piece.begin;
                    plan.arrival[*piece.source] = std::max(plan.arrival[*piece.source], piece.ready);
                }
                else
                {
                    plan.ready = std::max(plan.ready, piece.ready);
                }
            }
            for (std::size_t source = 0; source < plan.bytes.size(); ++source)
            {
                if (plan.bytes[source] == 0)
                    continue;
                plan.arrival[source] += model_->copy_time(source, device, plan.bytes[source]);
                plan.ready = std::max(plan.ready, plan.arrival[source]);
            }
            return plan;
        }

        double StepSimulation::earliest_start(std::size_t device, double ready, double duration) const
        {
            double candidate = ready;
            for (const Slot& slot : timelines_[device])
            {
                if (slot.end <= candidate)
                    continue;
                if (candidate + duration <= slot.start)
                    return candidate;
                candidate = std::max(candidate, slot.end);
            }
            return candidate;
        }

        double StepSimulation::predecessors_done(std::size_t node) const
        {
            double done = 0.0;
            for (const std::size_t edge : model_->edges_into(node))
                done = std::max(done, finish_[model_->problem().edges[edge].from]);
            return done;
        }

        void StepSimulation::materialize(std::size_t handle, std::uint64_t begin, std::uint64_t end)
        {
            split_at(handle, begin);
            split_at(handle, end);
            std::map<std::uint64_t, Segment>& segments = segments_[handle];
            std::uint64_t position = begin;
            while (position < end)
            {
                const auto it = segments.lower_bound(position);
                if (it != segments.end() && it->first == position)
                {
                    position = it->second.end;
                    continue;
                }
                const std::uint64_t gap_end = it == segments.end() ? end : std::min(end, it->first);
                segments.emplace(position, initial_segment(handle, gap_end));
                position = gap_end;
            }
        }

        void StepSimulation::split_at(std::size_t handle, std::uint64_t position)
        {
            std::map<std::uint64_t, Segment>& segments = segments_[handle];
            auto it = segments.upper_bound(position);
            if (it == segments.begin())
                return;
            --it;
            if (it->first >= position || it->second.end <= position)
                return;
            Segment tail = it->second;
            it->second.end = position;
            segments.emplace(position, std::move(tail));
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
