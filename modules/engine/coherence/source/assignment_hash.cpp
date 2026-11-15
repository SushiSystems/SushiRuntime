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

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            constexpr std::uint64_t K_FNV_OFFSET = 0xcbf29ce484222325ULL;
            constexpr std::uint64_t K_FNV_PRIME = 0x100000001b3ULL;

            /** @brief Folds the eight little-endian bytes of value into hash. */
            void fold(std::uint64_t& hash, std::uint64_t value) noexcept
            {
                for (unsigned shift = 0; shift < 64; shift += 8)
                {
                    hash ^= (value >> shift) & 0xffU;
                    hash *= K_FNV_PRIME;
                }
            }

            /** @brief Folds every field of key into hash. */
            void fold_key(std::uint64_t& hash, const DeviceKey& key) noexcept
            {
                fold(hash, static_cast<std::uint64_t>(key.backend));
                fold(hash, static_cast<std::uint64_t>(key.id_kind));
                for (const std::uint8_t byte : key.hardware_id)
                    fold(hash, byte);
                fold(hash, key.ordinal);
            }
        } // namespace

        std::uint64_t assignment_hash(const PartitionAssignment& assignment) noexcept
        {
            std::uint64_t hash = K_FNV_OFFSET;
            fold(hash, assignment.node_devices.size());
            for (const DeviceKey& key : assignment.node_devices)
                fold_key(hash, key);
            fold(hash, assignment.homes.size());
            for (const auto& home : assignment.homes)
            {
                fold(hash, home.first.to_high_word());
                fold(hash, home.first.to_low_word());
                fold_key(hash, home.second);
            }
            std::uint64_t step_bits = 0;
            static_assert(sizeof(step_bits) == sizeof(assignment.step_time), "a double is eight bytes");
            std::memcpy(&step_bits, &assignment.step_time, sizeof(step_bits));
            fold(hash, step_bits);
            fold(hash, assignment.floor_taken ? 1U : 0U);
            fold(hash, assignment.splits.size());
            for (const PlannedSplit& split : assignment.splits)
            {
                fold(hash, split.node);
                fold(hash, split.parts.size());
                for (const PlannedSplitPart& part : split.parts)
                {
                    fold_key(hash, part.device);
                    fold(hash, part.part.axis);
                    fold(hash, part.part.begin);
                    fold(hash, part.part.end);
                }
            }
            return hash;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
