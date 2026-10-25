/**************************************************************************/
/* worker_options_parser.hpp                                              */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file worker_options_parser.hpp
 * @brief Declares parse_worker_options(), which reads a worker's flags and environment.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/worker_main.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Names the callable that returns an environment variable's value.
         * @return Nothing when the variable is unset; an empty string when it is set to none.
         */
        using EnvironmentReader = std::function<std::optional<std::string>(const char* name)>;

        /**
         * @brief Names what a command line asks the worker process to do.
         */
        enum class WorkerOptionsStatus : std::uint8_t
        {
            Run,     /**< The options are complete; start the worker. */
            Help,    /**< `--help` was given; print the message and exit 0. */
            Version, /**< `--version` was given; print the version and exit 0. */
            Invalid, /**< A flag or a value is wrong; print the message and exit 1. */
        };

        /**
         * @brief Carries the outcome of parse_worker_options().
         */
        struct WorkerOptionsResult
        {
            /** @brief What the command line asks for. */
            WorkerOptionsStatus status = WorkerOptionsStatus::Run;

            /** @brief The settings; meaningful when status is Run. */
            WorkerOptions options;

            /** @brief One line naming what was wrong for Invalid; the usage text for Help. */
            std::string message;
        };

        /**
         * @brief Reads a worker's settings: a flag beats its variable, which beats the default.
         *
         * The first of `--help`, `--version` or a bad flag decides. A variable set to nothing
         * counts as unset. Labels combine: those of `SR_WORKER_LABELS` first, then each `--label`.
         * A label with an empty key, no '=', a repeated key or a part over 255 bytes is Invalid.
         * @param arguments The command line without the program's name; no entry is null.
         */
        [[nodiscard]] SUSHIRUNTIME_API WorkerOptionsResult parse_worker_options(
            SushiRuntime::span<const char* const> arguments,
            const EnvironmentReader& environment);

        /**
         * @brief Reads a worker's settings as the overload above does, from this process's
         *        environment.
         * @param arguments The command line without the program's name; no entry is null.
         */
        [[nodiscard]] SUSHIRUNTIME_API WorkerOptionsResult parse_worker_options(
            SushiRuntime::span<const char* const> arguments);

    } // namespace Distributed
} // namespace SushiRuntime
