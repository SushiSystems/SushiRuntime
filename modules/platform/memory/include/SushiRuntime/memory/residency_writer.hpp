/**************************************************************************/
/* residency_writer.hpp                                                   */
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
 * @file residency_writer.hpp
 * @brief Declares ResidencyWriter, the port that records instances and locks a buffer's coherence map.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <optional>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_status.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>
#include <SushiRuntime/memory/locked_coherence.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief What a planner or a step needs to change the residency directory: record and drop
         *        an instance, and hold a buffer's coherence map under its lock.
         *
         * ResidencyDirectory implements it. A consumer that also reads takes ResidencyReader.
         */
        class ResidencyWriter
        {
            public:
                /** @brief Frees nothing. */
                virtual ~ResidencyWriter() = default;

                /**
                 * @brief Returns the coherence map of a live handle under its entry's lock.
                 *
                 * The lock is held until the returned object is destroyed. The caller takes no
                 * other directory lock while it holds one.
                 * @return An empty object when handle is not live.
                 */
                [[nodiscard]] virtual LockedCoherence coherence(GlobalHandle handle) = 0;

                /**
                 * @brief Records address as handle's instance on space, with the kind of USM it is.
                 * @param kind The kind the instance was allocated as; nothing for memory the
                 *        runtime did not allocate.
                 * @return Ok, or the status that says why nothing was recorded.
                 */
                [[nodiscard]] virtual HandleStatus
                set_instance(GlobalHandle handle, MemorySpaceID space, void* address,
                             std::optional<USMKind> kind = std::nullopt) = 0;

                /** @brief Forgets handle's instance on space; the memory itself is not freed. */
                [[nodiscard]] virtual HandleStatus drop_instance(GlobalHandle handle,
                                                                 MemorySpaceID space) = 0;

            protected:
                /** @brief Builds the writer. */
                ResidencyWriter() = default;

                /** @brief Copies the writer. */
                ResidencyWriter(const ResidencyWriter&) = default;

                /** @brief Moves the writer. */
                ResidencyWriter(ResidencyWriter&&) = default;

                /** @brief Copies the writer. */
                ResidencyWriter& operator=(const ResidencyWriter&) = default;

                /** @brief Moves the writer. */
                ResidencyWriter& operator=(ResidencyWriter&&) = default;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
