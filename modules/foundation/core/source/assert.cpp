/**************************************************************************/
/* assert.cpp                                                             */
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
 * @file assert.cpp
 * @brief Holds the assertion handler and stops the process after calling it.
 * @author Mustafa Garip
 */

#include <SushiRuntime/core/assert.hpp>

#include <cstdio>
#include <cstdlib>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Writes the failure to standard error, on one line. */
        void write_failure_to_standard_error(const AssertionFailure& failure)
        {
            std::fprintf(stderr, "SushiRuntime: %s failed: %s (%s:%d)\n",
                         failure.expression != nullptr ? failure.expression : "fatal",
                         failure.message != nullptr ? failure.message : "",
                         failure.file != nullptr ? failure.file : "",
                         failure.line);
            std::fflush(stderr);
        }

        /** @brief Returns the slot the process-wide macros report to. */
        AssertionHandlerSlot& process_slot() noexcept
        {
            static AssertionHandlerSlot slot;
            return slot;
        }
    } // namespace

    AssertionHandlerSlot::AssertionHandlerSlot() noexcept
        : handler_(&write_failure_to_standard_error)
    {

    }

    AssertionHandler AssertionHandlerSlot::exchange(AssertionHandler handler) noexcept
    {
        if (handler == nullptr)
            handler = &write_failure_to_standard_error;
        return handler_.exchange(handler, std::memory_order_acq_rel);
    }

    void AssertionHandlerSlot::report(const AssertionFailure& failure) const noexcept
    {
        handler_.load(std::memory_order_acquire)(failure);
    }

    AssertionHandler set_assertion_handler(AssertionHandler handler) noexcept
    {
        return process_slot().exchange(handler);
    }

    void fail_assertion(const AssertionFailure& failure) noexcept
    {
        process_slot().report(failure);
        std::abort();
    }

} // namespace SushiRuntime
