/**************************************************************************/
/* partition_ledger.hpp                                                   */
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
 * @file partition_ledger.hpp
 * @brief Declares PartitionLedger, what the runs of a partitioned plan measured.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <vector>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device_profile/dispatch_observation.hpp>
#include <SushiRuntime/device_profile/profile_transfer_cost.hpp>
#include <SushiRuntime/partition_session/transfer_path_tally.hpp>
#include <SushiRuntime/partitioning/partition_measurement.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Holds the dispatches, steps and seconds the runs of one plan measured.
         *
         * The step driver adds to it after each run and folds it at a horizon, between runs, so
         * it takes no lock. The time per device sums the dispatches the device ran.
         */
        class PartitionLedger final
        {
            public:
                /** @brief Adds the dispatches of a run. */
                void add_dispatches(const std::vector<DispatchObservation>& observations);

                /** @brief Adds steps that took seconds in all. */
                void add_steps(std::size_t steps, double seconds) noexcept;

                /**
                 * @brief Returns the measured step, the device time of a step per device and the
                 *        copies of a step into each device, by path.
                 * @param copies    The copies of the same runs, by destination space and path.
                 * @param device_of Names the device of a space; a copy into a space with none is left out.
                 */
                [[nodiscard]] PartitionMeasurement measurement(const PathTallies& copies,
                                                               const SpaceDeviceResolver& device_of) const;

                /**
                 * @brief Folds the dispatches added since the last fold into the profile file at path.
                 *
                 * The dispatches are dropped once the file is written; the per-step figures stay.
                 * @return False when there was nothing to fold, or the error update_profile_file() gave.
                 */
                [[nodiscard]] Result<bool> fold_into(const std::filesystem::path& path);

                /** @brief Returns the dispatches added since the last call and forgets them, for a horizon's fold in memory. */
                [[nodiscard]] std::vector<DispatchObservation> take_horizon();

                /** @brief Forgets everything, for a plan made again. */
                void clear() noexcept;

            private:
                /// @brief Dispatches not yet folded into the profile file.
                std::vector<DispatchObservation> unfolded_;

                /// @brief Dispatches not yet taken by take_horizon().
                std::vector<DispatchObservation> horizon_;

                /// @brief Seconds of every dispatch since the last clear(), per device.
                std::map<DeviceKey, double> device_seconds_;

                /// @brief Steps since the last clear().
                std::size_t steps_ = 0;

                /// @brief Seconds of those steps.
                double seconds_ = 0.0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
