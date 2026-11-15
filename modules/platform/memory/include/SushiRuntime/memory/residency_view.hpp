/**************************************************************************/
/* residency_view.hpp                                                     */
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
 * @file residency_view.hpp
 * @brief Declares ResidencyView, the port through which a reader and a planner see the directory.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_resolver.hpp>
#include <SushiRuntime/handle/handle_status.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>
#include <SushiRuntime/memory/coherence_intervals.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /** @brief Holds the size and alignment of one element of a buffer. */
        struct ElementLayout
        {
            std::size_t size = 1;      /**< Bytes per element. */
            std::size_t alignment = 1; /**< Alignment of an element in bytes. */
        };

        /** @brief Holds what a reader sees of one live entry. */
        struct EntryView
        {
            std::size_t size = 0;                   /**< Bytes of the buffer. */
            ElementLayout layout;                   /**< Layout of one element. */
            MemorySpaceID home;                     /**< Space the buffer was made on. */
            std::uint64_t valid_spaces = 0;         /**< Bit i set when space i holds an instance. */
            HandleStatus status = HandleStatus::Ok; /**< Ok, or why nothing was read. */
            std::optional<USMKind> home_kind;       /**< Kind of the home instance; nothing when unknown. */
        };

        /**
         * @brief What a planner needs of the residency directory: resolve, inspect, the coherence
         *        map, and the recording of instances.
         *
         * ResidencyDirectory implements it; a test fake may too. The reading calls take no lock.
         */
        class ResidencyView : public HandleResolver
        {
            public:
                /** @brief Frees nothing. */
                ~ResidencyView() override = default;

                /** @brief Returns the size, layout, home and valid spaces of a live handle. */
                [[nodiscard]] virtual EntryView inspect(GlobalHandle handle) const = 0;

                /**
                 * @brief Returns the coherence map of a live handle, or null when it is not live.
                 *
                 * The planner is the map's only writer, at plan time; the map takes no lock.
                 */
                [[nodiscard]] virtual CoherenceIntervals* coherence(GlobalHandle handle) noexcept = 0;

                /** @brief Returns the coherence map of a live handle, or null when it is not live. */
                [[nodiscard]] virtual const CoherenceIntervals*
                coherence(GlobalHandle handle) const noexcept = 0;

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

                /**
                 * @brief Returns the kind of handle's instance on space.
                 * @return Nothing when handle is not live, has no instance on space, or the
                 *         instance's kind is unknown.
                 */
                [[nodiscard]] virtual std::optional<USMKind> instance_kind(GlobalHandle handle,
                                                                           MemorySpaceID space) const = 0;
        };

        /**
         * @brief Returns the ErrorCode a failed handle operation is reported with.
         * @pre status is not HandleStatus::Ok.
         */
        [[nodiscard]] ErrorCode error_code_of(HandleStatus status) noexcept;

        /** @brief Returns the resolved address, or the Error the outcome's status maps to. */
        [[nodiscard]] Result<void*> to_result(const ResolveOutcome& outcome);
    } // namespace Internal::Memory
} // namespace SushiRuntime
