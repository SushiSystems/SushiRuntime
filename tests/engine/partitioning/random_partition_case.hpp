/**************************************************************************/
/* random_partition_case.hpp                                              */
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
 * @file random_partition_case.hpp
 * @brief Declares the seeded graph generator and the exhaustive placement search the partitioner property tests share.
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <SushiRuntime/partitioning/partition_assignment.hpp>
#include <SushiRuntime/partitioning/partition_model.hpp>

#include "support/partition_problem_builder.hpp"

namespace SushiRuntime::Internal::Coherence
{
    /** @brief Draws numbers from a seed with a portable generator, so a seed names one graph on every platform. */
    class SeededDraw
    {
        public:
            /** @brief Starts the sequence of seed. */
            explicit SeededDraw(std::uint64_t seed) noexcept : state_(seed)
            {
            }

            /** @brief Returns the next number of the sequence. */
            [[nodiscard]] std::uint64_t next() noexcept
            {
                state_ += 0x9E3779B97F4A7C15ULL;
                std::uint64_t mixed = state_;
                mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ULL;
                mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBULL;
                return mixed ^ (mixed >> 31);
            }

            /** @brief Returns a number in [low, high]. */
            [[nodiscard]] std::uint64_t between(std::uint64_t low, std::uint64_t high) noexcept
            {
                return low + next() % (high - low + 1);
            }

        private:
            /// @brief The generator state.
            std::uint64_t state_;
    };

    /** @brief Holds a generated problem and the speed of each of its devices. */
    struct RandomPartitionCase
    {
        PartitionProblem problem;    /**< The generated graph and its devices. */
        std::vector<double> speeds;  /**< Speed of device i, whose ordinal is i + 1. */
    };

    /**
     * @brief Returns the case of seed: three to eight nodes, forward edges only, and the given speeds.
     *
     * Node i writes buffer i and reads the buffers of its one or two predecessors. A buffer holds
     * 50 to max_buffer_bytes, so a bound of 100 puts every buffer in one round_profile pool.
     */
    [[nodiscard]] inline RandomPartitionCase random_partition_case(std::uint64_t seed, std::vector<double> speeds,
                                                                   std::uint64_t max_buffer_bytes)
    {
        SeededDraw draw(seed);
        RandomPartitionCase made;
        made.speeds = std::move(speeds);
        add_devices(made.problem, made.speeds.size());
        const std::size_t count = static_cast<std::size_t>(draw.between(3, 8));
        for (std::size_t node = 0; node < count; ++node)
        {
            PartitionNode entry;
            entry.cost.operations = draw.between(1000, 9000);
            entry.cost.read_bytes = draw.between(0, 2000);
            entry.cost.written_bytes = draw.between(0, 2000);
            entry.cost.kind = draw.between(0, 1) == 0 ? OperationKinds::K_GENERIC : OperationKinds::K_COMPUTE_BOUND;
            entry.writes.push_back(add_buffer(made.problem, draw.between(50, max_buffer_bytes)));
            if (node > 0)
            {
                const std::size_t first = static_cast<std::size_t>(draw.between(0, node - 1));
                const std::size_t second = static_cast<std::size_t>(draw.between(0, node - 1));
                entry.reads.push_back(first);
                made.problem.edges.push_back(PartitionEdge{first, node, draw.between(100, 400)});
                if (second != first)
                {
                    entry.reads.push_back(second);
                    made.problem.edges.push_back(PartitionEdge{second, node, draw.between(100, 400)});
                }
            }
            made.problem.nodes.push_back(entry);
        }
        return made;
    }

    /**
     * @brief Returns the least modelled step time over every placement of model that fits and keeps the fixed nodes.
     * @return Nothing when no placement fits.
     */
    [[nodiscard]] inline std::optional<double> best_feasible_step_time(const PartitionModel& model)
    {
        const std::size_t devices = model.device_count();
        std::vector<std::size_t> placement(model.node_count(), 0);
        std::optional<double> best;
        bool more = true;
        while (more)
        {
            bool keeps_fixed = true;
            for (std::size_t node = 0; node < placement.size(); ++node)
                keeps_fixed = keeps_fixed && (!model.fixed_device(node) || *model.fixed_device(node) == placement[node]);
            if (keeps_fixed && model.fits(placement))
            {
                const double time = model.step_time(placement);
                if (!best || time < *best)
                    best = time;
            }
            std::size_t digit = 0;
            while (digit < placement.size() && ++placement[digit] == devices)
                placement[digit++] = 0;
            more = digit < placement.size();
        }
        return best;
    }

    /** @brief Returns the device index of model that each node of assignment sits on. */
    [[nodiscard]] inline std::vector<std::size_t> placement_of(const PartitionModel& model,
                                                               const PartitionAssignment& assignment)
    {
        std::vector<std::size_t> placement;
        for (const DeviceKey& key : assignment.node_devices)
        {
            std::size_t device = 0;
            while (!(model.device_key(device) == key))
                ++device;
            placement.push_back(device);
        }
        return placement;
    }
} // namespace SushiRuntime::Internal::Coherence
