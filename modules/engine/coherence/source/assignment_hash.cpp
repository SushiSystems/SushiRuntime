/**************************************************************************/
/* assignment_hash.cpp                                                    */
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
 * @file assignment_hash.cpp
 * @brief Implements assignment_hash.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/assignment_hash.hpp>

#include <cstring>

#include <SushiRuntime/coherence/fnv_hash.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Folds every field of key into hash. */
            void fold_key(FNVHash& hash, const DeviceKey& key) noexcept
            {
                hash.fold(static_cast<std::uint64_t>(key.backend));
                hash.fold(static_cast<std::uint64_t>(key.id_kind));
                for (const std::uint8_t byte : key.hardware_id)
                    hash.fold(byte);
                hash.fold(key.ordinal);
            }
        } // namespace

        std::uint64_t assignment_hash(const PartitionAssignment& assignment) noexcept
        {
            FNVHash hash;
            hash.fold(assignment.node_devices.size());
            for (const DeviceKey& key : assignment.node_devices)
                fold_key(hash, key);
            hash.fold(assignment.homes.size());
            for (const auto& home : assignment.homes)
            {
                hash.fold(home.first.to_high_word());
                hash.fold(home.first.to_low_word());
                fold_key(hash, home.second);
            }
            std::uint64_t step_bits = 0;
            static_assert(sizeof(step_bits) == sizeof(assignment.step_time), "a double is eight bytes");
            std::memcpy(&step_bits, &assignment.step_time, sizeof(step_bits));
            hash.fold(step_bits);
            hash.fold(assignment.floor_taken ? 1U : 0U);
            hash.fold(assignment.splits.size());
            for (const PlannedSplit& split : assignment.splits)
            {
                hash.fold(split.node);
                hash.fold(split.parts.size());
                for (const PlannedSplitPart& part : split.parts)
                {
                    fold_key(hash, part.device);
                    hash.fold(part.part.axis);
                    hash.fold(part.part.begin);
                    hash.fold(part.part.end);
                }
            }
            return hash.value();
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
