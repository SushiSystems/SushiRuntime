/**************************************************************************/
/* error.hpp                                                              */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <cstdint>
#include <source_location>
#include <stdexcept>
#include <string>
#include <utility>

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Categorises every failure the runtime can report.
         *
         * One code per failure class so callers can branch on the cause without
         * parsing the message. Synchronous failures (graph, compile, allocation)
         * surface at the call site; device faults arrive through the queue's
         * async handler and map to kernel_failed.
         */
        enum class Errc : std::uint8_t
        {
            ok,                 /**< No error. */
            invalid_graph,      /**< The graph is empty, cyclic, or otherwise unrunnable. */
            compile_failed,     /**< Dependency analysis or graph compilation failed. */
            allocation_failed,  /**< A USM or host allocation could not be satisfied. */
            kernel_failed,      /**< A kernel faulted on the device (async channel). */
            device_lost         /**< The target device disappeared or became unusable. */
        };

        /**
         * @brief Maps an error code to a short, stable English label.
         *
         * Used when building messages and when formatting errors for logs. The
         * returned pointer is a string literal and is always non-null.
         *
         * @param code The error code to describe.
         * @return A short label, e.g. "kernel_failed".
         */
        inline const char* to_string(Errc code) noexcept
        {
            switch (code)
            {
                case Errc::ok:                return "ok";
                case Errc::invalid_graph:     return "invalid_graph";
                case Errc::compile_failed:    return "compile_failed";
                case Errc::allocation_failed: return "allocation_failed";
                case Errc::kernel_failed:     return "kernel_failed";
                case Errc::device_lost:       return "device_lost";
            }
            return "unknown";
        }

        /**
         * @brief A single failure: its category, a human message, and its origin.
         *
         * This is the value carried by the non-throwing channel
         * (std::expected<RunReport, Error>). The source location defaults to the
         * point of construction so the failure records where it was raised.
         */
        struct Error
        {
            Errc code = Errc::ok;                       /**< Failure category. */
            std::string message;                        /**< Human-readable detail. */
            std::source_location where =                /**< Where the error was raised. */
                std::source_location::current();
        };

        /**
         * @brief Exception form of Error, thrown by the throwing entry points.
         *
         * run() throws this; try_run() returns the equivalent Error instead. The
         * what() text is the Error's message so existing catch(const
         * std::exception&) sites keep working.
         */
        class SushiException : public std::runtime_error
        {
            public:
                /**
                 * @brief Wraps an Error as a throwable exception.
                 * @param err The failure to carry.
                 */
                explicit SushiException(Error err)
                    : std::runtime_error(err.message), err_(std::move(err))
                {
                }

                /** @brief The structured error behind this exception. */
                const Error& error() const noexcept { return err_; }

            private:
                Error err_; /**< Structured failure detail. */
        };

        /**
         * @brief Summary returned by a successful run.
         *
         * Populated by the fluent layer once a graph finishes. Kept minimal and
         * stable; fields are added as the execution engine exposes more telemetry.
         */
        struct RunReport
        {
            std::uint64_t tasks_executed = 0;   /**< Number of tasks dispatched and completed. */
            double duration_ms = 0.0;           /**< Wall-clock time of the run in milliseconds. */
        };

    } // namespace Core
} // namespace SushiRuntime
