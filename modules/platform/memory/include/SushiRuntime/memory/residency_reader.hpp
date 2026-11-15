/**************************************************************************/
/* residency_reader.hpp                                                   */
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
 * @file residency_reader.hpp
 * @brief Declares ResidencyReader, the port that reads the residency directory and changes nothing.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/handle/element_layout.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_home_reader.hpp>
#include <SushiRuntime/handle/handle_status.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /** @brief Names the layout of one element of a buffer, declared in the handle module. */
        using SushiRuntime::ElementLayout;

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
         * @brief What a consumer needs to read the residency directory: resolve, inspect, the home
         *        of a handle and the kind of an instance.
         *
         * ResidencyDirectory implements it; a consumer that changes the directory takes
         * ResidencyWriter as well. The calls take no lock.
         */
        class ResidencyReader : public HandleHomeReader
        {
            public:
                /** @brief Frees nothing. */
                ~ResidencyReader() override = default;

                /** @brief Returns the size, layout, home and valid spaces of a live handle. */
                [[nodiscard]] virtual EntryView inspect(GlobalHandle handle) const = 0;

                /**
                 * @brief Returns the kind of handle's instance on space.
                 * @return Nothing when handle is not live, has no instance on space, or the
                 *         instance's kind is unknown.
                 */
                [[nodiscard]] virtual std::optional<USMKind> instance_kind(GlobalHandle handle,
                                                                           MemorySpaceID space) const = 0;

                /** @brief Returns the home of handle, read from inspect(). */
                [[nodiscard]] HandleHome home_of(GlobalHandle handle) const final;
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
