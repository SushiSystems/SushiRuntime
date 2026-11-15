/**************************************************************************/
/* partition_ledger.cpp                                                   */
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
 * @file partition_ledger.cpp
 * @brief Implements PartitionLedger.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/partition_ledger.hpp>

#include <optional>

#include <SushiRuntime/coherence/profile_update.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        void PartitionLedger::add_dispatches(const std::vector<DispatchObservation>& observations)
        {
            unfolded_.insert(unfolded_.end(), observations.begin(), observations.end());
            for (const DispatchObservation& observation : observations)
                device_seconds_[observation.device] += observation.seconds;
        }

        void PartitionLedger::add_steps(std::size_t steps, double seconds) noexcept
        {
            steps_ += steps;
            seconds_ += seconds;
        }

        PartitionMeasurement PartitionLedger::measurement(const PathTallies& copies,
                                                          const SpaceDeviceResolver& device_of) const
        {
            PartitionMeasurement measured;
            if (steps_ == 0)
                return measured;
            const auto steps = static_cast<double>(steps_);
            measured.step_seconds = seconds_ / steps;
            for (const auto& entry : device_seconds_)
                measured.device_seconds[entry.first] = entry.second / steps;
            // The tallies are ordered by space, then path, so each device's rows come out in path order.
            for (const auto& entry : copies)
            {
                const std::optional<DeviceKey> device = device_of ? device_of(entry.first.first) : std::nullopt;
                if (!device)
                    continue;
                measured.copies[*device].push_back(PartitionCopyRow{
                    entry.first.second, static_cast<double>(entry.second.copies) / steps,
                    static_cast<double>(entry.second.bytes) / steps});
            }
            return measured;
        }

        Result<bool> PartitionLedger::fold_into(const std::filesystem::path& path)
        {
            if (unfolded_.empty())
                return false;
            Result<bool> written = update_profile_file(path, unfolded_);
            if (written)
                unfolded_.clear();
            return written;
        }

        void PartitionLedger::clear() noexcept
        {
            unfolded_.clear();
            device_seconds_.clear();
            steps_ = 0;
            seconds_ = 0.0;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
