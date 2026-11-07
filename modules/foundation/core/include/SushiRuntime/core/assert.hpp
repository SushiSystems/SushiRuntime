/**************************************************************************/
/* assert.hpp                                                             */
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
 * @file assert.hpp
 * @brief Declares SR_ASSERT, SR_VERIFY and SR_FATAL and the handler a failed check calls.
 * @author Mustafa Garip
 */

#pragma once

#include <atomic>

#include <SushiRuntime/core/export.hpp>

/**
 * @brief Selects whether SR_ASSERT checks its condition; 1 without NDEBUG, 0 with it.
 * @see modules/foundation/core/README.md
 */
#ifndef SR_ENABLE_ASSERTIONS
#    ifdef NDEBUG
#        define SR_ENABLE_ASSERTIONS 0
#    else
#        define SR_ENABLE_ASSERTIONS 1
#    endif
#endif

/**
 * @brief Stops the process when the condition is false, in every build.
 * @param message A C string; it is evaluated only when the check fails.
 */
#define SR_VERIFY(condition, message)                                                  \
    do                                                                                 \
    {                                                                                  \
        if (!(condition))                                                              \
            ::SushiRuntime::fail_assertion(::SushiRuntime::AssertionFailure{           \
                #condition, (message), __FILE__, __LINE__});                           \
    } while (false)

/**
 * @brief Stops the process, in every build.
 * @param message A C string that says which state was reached.
 */
#define SR_FATAL(message)                                                              \
    ::SushiRuntime::fail_assertion(                                                    \
        ::SushiRuntime::AssertionFailure{nullptr, (message), __FILE__, __LINE__})

#if SR_ENABLE_ASSERTIONS
/**
 * @brief Stops the process when the condition is false, where SR_ENABLE_ASSERTIONS is 1.
 * @param message A C string; it is evaluated only when the check fails.
 */
#    define SR_ASSERT(condition, message) SR_VERIFY(condition, message)
#else
/**
 * @brief Stops the process when the condition is false, where SR_ENABLE_ASSERTIONS is 1.
 * @param message A C string; it is evaluated only when the check fails.
 */
#    define SR_ASSERT(condition, message)                                              \
        do                                                                             \
        {                                                                              \
            (void)sizeof(!(condition));                                                \
        } while (false)
#endif

namespace SushiRuntime
{
    /**
     * @brief Holds what a failed check knows about itself.
     */
    struct AssertionFailure
    {
        const char* expression = nullptr; /**< The condition's text; null for SR_FATAL. */
        const char* message = nullptr;    /**< The rule the caller was to keep. */
        const char* file = nullptr;       /**< The source file of the check. */
        int line = 0;                     /**< The line of the check. */
    };

    /**
     * @brief Names the function a failed check calls before the process stops.
     */
    using AssertionHandler = void (*)(const AssertionFailure& failure);

    /**
     * @brief Holds one assertion handler and calls it, without stopping the process.
     *
     * The process keeps one slot, which set_assertion_handler() and fail_assertion() use. A
     * caller that owns a slot sees a failure reported and the call return.
     */
    class AssertionHandlerSlot final
    {
        public:
            /** @brief Holds the handler that writes the failure to standard error. */
            SUSHIRUNTIME_API AssertionHandlerSlot() noexcept;

            /**
             * @brief Holds @p handler and returns the one it replaces.
             * @param handler Null restores the handler that writes the failure to standard error.
             */
            SUSHIRUNTIME_API AssertionHandler exchange(AssertionHandler handler) noexcept;

            /** @brief Calls the held handler with @p failure and returns. */
            SUSHIRUNTIME_API void report(const AssertionFailure& failure) const noexcept;

        private:
            /// @brief The held handler; never null.
            std::atomic<AssertionHandler> handler_;
    };

    /**
     * @brief Installs the handler a failed check calls and returns the one it replaces.
     * @param handler Null restores the handler that writes the failure to standard error.
     */
    SUSHIRUNTIME_API AssertionHandler set_assertion_handler(AssertionHandler handler) noexcept;

    /**
     * @brief Calls the installed handler with the failure, then aborts the process.
     */
    [[noreturn]] SUSHIRUNTIME_API void fail_assertion(const AssertionFailure& failure) noexcept;

} // namespace SushiRuntime
