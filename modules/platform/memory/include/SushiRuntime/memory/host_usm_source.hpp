/**************************************************************************/
/* host_usm_source.hpp                                                    */
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
 * @file host_usm_source.hpp
 * @brief Declares HostUSMSource, the in-memory USMSource that allocates from the host heap.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/usm_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Allocates every kind of USM from the host heap, for a fixed number of devices.
         *
         * Every device shares one context, and an address the allocator did not record has no
         * owner. It makes no device and starts no thread, so a small test can use it.
         * @see modules/platform/memory/README.md
         */
        class SUSHIRUNTIME_API HostUSMSource final : public USMSource
        {
            public:
                /**
                 * @brief Makes a source of a number of host devices.
                 * @param device_count At least 1.
                 */
                explicit HostUSMSource(std::size_t device_count = 1);

                [[nodiscard]] std::size_t device_count() const noexcept override;
                [[nodiscard]] void* allocate(const USMRequest& request) override;
                void free(void* address, std::size_t device_index) override;
                [[nodiscard]] std::optional<std::size_t>
                resolve_owner(const void* address) const override;
                [[nodiscard]] bool shares_context(std::size_t first,
                                                  std::size_t second) const override;
                [[nodiscard]] std::string device_name(std::size_t index) const override;

            private:
                /** @brief The number of devices the source stands for. */
                std::size_t device_count_;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
