/**************************************************************************/
/* partition_problem_builder.hpp                                          */
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
 * @file partition_problem_builder.hpp
 * @brief Declares the problem and profile builders the partitioner tests share.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <SushiRuntime/device_profile/cost_model.hpp>
#include <SushiRuntime/partitioning/partition_problem.hpp>

#include "support/fake_profile_probe.hpp"

namespace SushiRuntime::Internal::Coherence
{
    /** @brief Returns a cost model with one round_profile per speed, device ordinals counting from one. */
    [[nodiscard]] inline std::shared_ptr<const CostModel> speed_model(const std::vector<double>& speeds)
    {
        DeviceProfileSet profiles;
        for (std::size_t index = 0; index < speeds.size(); ++index)
            profiles.set(host_key(static_cast<std::uint32_t>(index + 1)), round_profile(speeds[index]));
        return std::make_shared<const CostModel>(profiles);
    }

    /** @brief Adds count devices with ordinals from one, each in a pool of its own and with the "pcie" path. */
    inline void add_devices(PartitionProblem& problem, std::size_t count)
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto ordinal = static_cast<std::uint32_t>(index + 1);
            problem.devices.push_back(PartitionDevice{host_key(ordinal), ordinal, TransferPathID::named("pcie")});
        }
    }

    /** @brief Adds a planned buffer of bytes and returns its index. */
    inline std::size_t add_buffer(PartitionProblem& problem, std::uint64_t bytes)
    {
        const std::size_t index = problem.buffers.size();
        problem.buffers.push_back(PartitionBuffer{GlobalHandle::from_words(1, index + 1), bytes, std::nullopt});
        return index;
    }

    /**
     * @brief Returns a chain of length nodes over devices pools; node i writes buffer i and reads buffer i - 1.
     *
     * Each buffer holds buffer_bytes, each node does 1000 operations, and each edge carries one buffer.
     */
    [[nodiscard]] inline PartitionProblem chain(std::size_t length, std::uint64_t buffer_bytes, std::size_t devices)
    {
        PartitionProblem problem;
        add_devices(problem, devices);
        for (std::size_t node = 0; node < length; ++node)
        {
            PartitionNode entry;
            entry.cost.operations = 1000;
            entry.writes.push_back(add_buffer(problem, buffer_bytes));
            if (node > 0)
            {
                entry.reads.push_back(node - 1);
                problem.edges.push_back(PartitionEdge{node - 1, node, buffer_bytes});
            }
            problem.nodes.push_back(entry);
        }
        return problem;
    }

    /**
     * @brief Returns a split of node along axis whose parts read the node's buffers whole and write
     *        the contiguous bytes [bytes * begin / extent, bytes * end / extent) of each written buffer.
     */
    [[nodiscard]] inline PartitionSplit sliced_split(const PartitionProblem& problem, std::size_t node,
                                                     std::uint32_t axis, std::uint64_t extent)
    {
        std::vector<PartitionBuffer> reads;
        for (const std::size_t buffer : problem.nodes[node].reads)
            reads.push_back(problem.buffers[buffer]);
        std::vector<PartitionBuffer> writes;
        for (const std::size_t buffer : problem.nodes[node].writes)
            writes.push_back(problem.buffers[buffer]);
        PartitionSplit split{axis, extent};
        split.regions = [reads, writes, extent](const SplitPart& part)
        {
            SplitRegions regions;
            for (const PartitionBuffer& buffer : reads)
                regions.reads.push_back(BufferRegion(buffer.handle, 0, static_cast<std::size_t>(buffer.bytes)));
            for (const PartitionBuffer& buffer : writes)
            {
                const std::uint64_t begin = buffer.bytes * part.begin / extent;
                const std::uint64_t end = buffer.bytes * part.end / extent;
                regions.writes.push_back(BufferRegion(buffer.handle, static_cast<std::size_t>(begin),
                                                      static_cast<std::size_t>(end - begin)));
            }
            return regions;
        };
        return split;
    }

    /** @brief Returns count nodes of 1000 operations over devices pools, each writing a buffer of 100 bytes of its own. */
    [[nodiscard]] inline PartitionProblem independent_nodes(std::size_t count, std::size_t devices)
    {
        PartitionProblem problem;
        add_devices(problem, devices);
        for (std::size_t node = 0; node < count; ++node)
        {
            PartitionNode entry;
            entry.cost.operations = 1000;
            entry.writes.push_back(add_buffer(problem, 100));
            problem.nodes.push_back(entry);
        }
        return problem;
    }

    /**
     * @brief Returns chain(length, buffer_bytes, devices) in which node i also writes buffer i - 1.
     *
     * The last node that touches each buffer writes it, so every buffer is carried to the next
     * step and live for the whole step.
     */
    [[nodiscard]] inline PartitionProblem state_chain(std::size_t length, std::uint64_t buffer_bytes, std::size_t devices)
    {
        PartitionProblem problem = chain(length, buffer_bytes, devices);
        for (std::size_t node = 1; node < length; ++node)
            problem.nodes[node].writes.push_back(node - 1);
        return problem;
    }
} // namespace SushiRuntime::Internal::Coherence
