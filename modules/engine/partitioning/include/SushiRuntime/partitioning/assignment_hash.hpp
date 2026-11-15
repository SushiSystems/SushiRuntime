/**************************************************************************/
/* assignment_hash.hpp                                                    */
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
 * @file assignment_hash.hpp
 * @brief Declares assignment_hash(), the deterministic hash of a PartitionAssignment.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

#include <SushiRuntime/partitioning/partition_assignment.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Returns the 64-bit FNV-1a hash of assignment's devices, homes, step time and floor flag.
         *
         * Every field is hashed as little-endian bytes in a fixed order, so two assignments of one
         * graph and one profile file hash equal in every run.
         */
        [[nodiscard]] std::uint64_t assignment_hash(const PartitionAssignment& assignment) noexcept;
    } // namespace Internal::Coherence
} // namespace SushiRuntime
