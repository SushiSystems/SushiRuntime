/**************************************************************************/
/* partition_model.hpp                                                    */
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
 * @file partition_model.hpp
 * @brief Declares PartitionModel, which prices and checks a placement of a PartitionProblem.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <SushiRuntime/coherence/cost_model.hpp>
#include <SushiRuntime/coherence/partition_problem.hpp>
#include <SushiRuntime/coherence/partition_refusal.hpp>
#include <SushiRuntime/coherence/pool_occupancy.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Prices a placement in modelled seconds and counts its bytes per pool.
         *
         * A placement holds one device index per node. Devices are the profiled ones in DeviceKey
         * order. A pool's usable bytes come from its first device's profile, so a shared pool counts
         * once. A step runs the nodes in recording order; an edge between two pools waits for the
         * copy over the source device's path.
         */
        class PartitionModel
        {
            public:
                /** @brief The most devices a model takes, so the floor's subsets stay few. */
                static constexpr std::size_t K_MAX_DEVICES = 16;

                /**
                 * @brief Builds the model of problem priced by costs.
                 *
                 * Refuses a malformed problem with InvalidArgument, work on an unprofiled device with
                 * InvalidState, and more than K_MAX_DEVICES profiled devices with Unsupported.
                 * @param problem The problem; must outlive the model.
                 * @param costs   The frozen cost model; must outlive the model.
                 */
                [[nodiscard]] static Result<PartitionModel, PartitionRefusal> build(const PartitionProblem& problem,
                                                                                    const CostModel& costs);

                /** @brief Returns the problem the model was built over. */
                [[nodiscard]] const PartitionProblem& problem() const noexcept
                {
                    return *problem_;
                }

                /** @brief Returns the number of nodes. */
                [[nodiscard]] std::size_t node_count() const noexcept
                {
                    return problem_->nodes.size();
                }

                /** @brief Returns the number of profiled devices. */
                [[nodiscard]] std::size_t device_count() const noexcept
                {
                    return devices_.size();
                }

                /** @brief Returns the number of pools. */
                [[nodiscard]] std::size_t pool_count() const noexcept
                {
                    return pool_ids_.size();
                }

                /** @brief Returns the key of device. */
                [[nodiscard]] const DeviceKey& device_key(std::size_t device) const
                {
                    return devices_[device];
                }

                /** @brief Returns the pool index of device. */
                [[nodiscard]] std::size_t pool_of(std::size_t device) const
                {
                    return device_pool_[device];
                }

                /** @brief Returns the usable bytes of pool. */
                [[nodiscard]] std::uint64_t usable_bytes(std::size_t pool) const
                {
                    return usable_[pool];
                }

                /** @brief Returns the device node is fixed to, or nothing when the partitioner may move it. */
                [[nodiscard]] std::optional<std::size_t> fixed_device(std::size_t node) const
                {
                    return fixed_[node];
                }

                /** @brief Returns the modelled seconds of node on device. */
                [[nodiscard]] double node_time(std::size_t node, std::size_t device) const
                {
                    return node_times_[node * devices_.size() + device];
                }

                /** @brief Returns the modelled seconds edge takes from source_device to target_device. */
                [[nodiscard]] double edge_time(std::size_t edge, std::size_t source_device,
                                               std::size_t target_device) const;

                /** @brief Returns the indices of the edges into node. */
                [[nodiscard]] const std::vector<std::size_t>& edges_into(std::size_t node) const
                {
                    return edges_into_[node];
                }

                /** @brief Returns the indices of the edges out of node. */
                [[nodiscard]] const std::vector<std::size_t>& edges_out_of(std::size_t node) const
                {
                    return edges_out_of_[node];
                }

                /** @brief Returns an occupancy that holds only the buffers with a stated device. */
                [[nodiscard]] PoolOccupancy empty_occupancy() const
                {
                    return PoolOccupancy(*problem_, pool_ids_.size(), buffer_pool_);
                }

                /** @brief Returns the modelled seconds of one step under placement. */
                [[nodiscard]] double step_time(const std::vector<std::size_t>& placement) const;

                /** @brief Returns, per pool in pool order, the bytes placement puts there and the usable bytes. */
                [[nodiscard]] std::vector<PoolShortfall> pool_demand(const std::vector<std::size_t>& placement) const;

                /** @brief Returns whether placement keeps every pool within its usable bytes. */
                [[nodiscard]] bool fits(const std::vector<std::size_t>& placement) const;

            private:
                /** @brief Builds an empty model over problem. */
                PartitionModel(const PartitionProblem& problem, const CostModel& costs)
                    : problem_(&problem), costs_(&costs)
                {

                }

                /// @brief The problem.
                const PartitionProblem* problem_;

                /// @brief The cost model that prices copies.
                const CostModel* costs_;

                /// @brief Profiled devices in DeviceKey order.
                std::vector<DeviceKey> devices_;

                /// @brief Copy path of each device.
                std::vector<std::string> copy_paths_;

                /// @brief Pool index of each device.
                std::vector<std::size_t> device_pool_;

                /// @brief PartitionDevice::pool of each pool index.
                std::vector<std::uint32_t> pool_ids_;

                /// @brief Usable bytes of each pool index.
                std::vector<std::uint64_t> usable_;

                /// @brief Device each node is fixed to.
                std::vector<std::optional<std::size_t>> fixed_;

                /// @brief Pool of each buffer's stated home.
                std::vector<std::optional<std::size_t>> buffer_pool_;

                /// @brief Seconds of node n on device d, at n * device_count() + d.
                std::vector<double> node_times_;

                /// @brief Edges into each node.
                std::vector<std::vector<std::size_t>> edges_into_;

                /// @brief Edges out of each node.
                std::vector<std::vector<std::size_t>> edges_out_of_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
