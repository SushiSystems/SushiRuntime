/**************************************************************************/
/* register_profiler_parts.hpp                                            */
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
 * @file register_profiler_parts.hpp
 * @brief Declares RuntimeProfilerParts and register_profiler_parts, how a host registers the runtime with SushiProfiler.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <memory>

#include <SushiProfiler/registry/registry.hpp>

#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    /**
     * @brief Owns what the runtime registers with a SushiProfiler registry.
     *
     * Neither copyable nor movable; the host keeps it until every session has stopped. A session
     * adds an observer to each runtime and removes it at its end; the destructor removes only its own.
     * @see modules/surface/profiling/README.md
     */
    class RuntimeProfilerParts final
    {
        public:
            /**
             * @brief Registers the source `sushiruntime.device` and the environment contributor for @p runtime.
             *
             * The registry refuses a source name twice; then is_registered() is false and nothing was added.
             * The object shares ownership of the runtime's context until it is destroyed.
             */
            SUSHIRUNTIME_API RuntimeProfilerParts(SushiProfiler::Registry& registry, const Runtime& runtime);

            RuntimeProfilerParts(const RuntimeProfilerParts&) = delete;
            RuntimeProfilerParts& operator=(const RuntimeProfilerParts&) = delete;
            RuntimeProfilerParts(RuntimeProfilerParts&&) = delete;
            RuntimeProfilerParts& operator=(RuntimeProfilerParts&&) = delete;

            /** @brief Removes the observers it added and releases its runtimes. */
            SUSHIRUNTIME_API ~RuntimeProfilerParts();

            /**
             * @brief Feeds the same source from a further runtime, so one timeline holds every runtime of the process.
             * @return False, adding nothing, while a session runs or when the registration was refused.
             */
            SUSHIRUNTIME_API bool attach(const Runtime& runtime);

            /** @brief Returns whether the registry accepted the source. */
            [[nodiscard]] SUSHIRUNTIME_API bool is_registered() const noexcept;

        private:
            struct Impl;

            /** @brief What the registry refers to; stable in memory. */
            std::unique_ptr<Impl> impl_;
    };

    /**
     * @brief Registers the runtime's source and environment contributor with @p registry.
     *
     * The pattern every SushiProfiler client library follows. The returned object is the caller's
     * variable itself, never a copy.
     * @see modules/surface/profiling/README.md
     */
    [[nodiscard]] SUSHIRUNTIME_API RuntimeProfilerParts register_profiler_parts(
        SushiProfiler::Registry& registry, const Runtime& runtime);
} // namespace SushiRuntime
