/**************************************************************************/
/* host_write_log.hpp                                                     */
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
 * @file host_write_log.hpp
 * @brief Declares HostWriteLog, the port that records and reads the host writes made outside a plan.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstdint>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_status.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Records which handles the host wrote outside a plan, under a rising generation.
         */
        class HostWriteLog
        {
            public:
                /**
                 * @brief Records that the host wrote bytes of handle, under the next generation.
                 * @return Ok, or the status of a handle that is not live.
                 */
                [[nodiscard]] virtual HandleStatus record_host_write(GlobalHandle handle) = 0;

                /** @brief Returns the generation of the last host write on any handle, or 0; any thread. */
                [[nodiscard]] virtual std::uint64_t host_write_generation() const noexcept = 0;

                /** @brief Returns the generation of the last host write on handle, or 0 for none or a stale handle. */
                [[nodiscard]] virtual std::uint64_t host_write_of(GlobalHandle handle) const noexcept = 0;

            protected:
                /** @brief Keeps a log from being destroyed through this interface. */
                ~HostWriteLog() = default;
        };

    } // namespace Internal::Memory
} // namespace SushiRuntime
