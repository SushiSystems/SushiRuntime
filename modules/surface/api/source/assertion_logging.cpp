/**************************************************************************/
/* assertion_logging.cpp                                                  */
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
 * @file assertion_logging.cpp
 * @brief Defines install_fatal_logging and the handler that writes a failed check to SushiLog.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#include <SushiRuntime/api/detail/assertion_logging.hpp>

#include <atomic>
#include <mutex>

#include <SushiRuntime/api/api_log_category.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/log/logger.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        namespace
        {
            /// @brief The handler that was installed before the logging one; it runs after it.
            std::atomic<AssertionHandler> previous_handler{nullptr};

            /// @brief Makes the installation happen once per process.
            std::once_flag installed;

            /** @brief Logs the failure at FATAL, then calls the handler it replaced. */
            void log_then_previous(const AssertionFailure& failure)
            {
                SR_LOG_FATAL(K_API_LOG_CATEGORY, "{} failed: {} ({}:{}); the process stops.",
                             failure.expression != nullptr ? failure.expression : "fatal",
                             failure.message != nullptr ? failure.message : "",
                             failure.file != nullptr ? failure.file : "", failure.line);

                const AssertionHandler previous = previous_handler.load(std::memory_order_acquire);
                if (previous != nullptr)
                    previous(failure);
            }
        } // namespace

        void install_fatal_logging() noexcept
        {
            std::call_once(installed,
                           []()
                           {
                               previous_handler.store(set_assertion_handler(&log_then_previous),
                                                      std::memory_order_release);
                           });
        }
    } // namespace Detail
} // namespace SushiRuntime
