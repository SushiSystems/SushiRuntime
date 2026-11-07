/**************************************************************************/
/* sycl_usm_source.hpp                                                    */
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
 * @file sycl_usm_source.hpp
 * @brief Declares SYCLUSMSource, which takes USM from a set of SYCL device queues.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/usm_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Allocates USM through sycl::malloc_* on one queue per device.
         * @see modules/platform/memory/README.md
         */
        class SUSHIRUNTIME_API SYCLUSMSource final : public USMSource
        {
            public:
                /**
                 * @brief Takes the queues to allocate on; a device's index is its position here.
                 * @param device_queues Copies that share state with the caller's queues.
                 */
                explicit SYCLUSMSource(std::vector<sycl::queue> device_queues);

                [[nodiscard]] std::size_t device_count() const noexcept override;
                [[nodiscard]] void* allocate(const USMRequest& request) override;
                void free(void* address, std::size_t device_index) override;
                [[nodiscard]] std::optional<std::size_t>
                resolve_owner(const void* address) const override;
                [[nodiscard]] bool shares_context(std::size_t first,
                                                  std::size_t second) const override;
                [[nodiscard]] std::string device_name(std::size_t index) const override;

            private:
                /** @brief Returns the queue of a device; fails on an index out of range. */
                [[nodiscard]] const sycl::queue& queue_at(std::size_t device_index) const;

                /** @brief The queues, one per device, in device order. */
                std::vector<sycl::queue> queues_;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
