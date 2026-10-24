/**************************************************************************/
/* sushiruntime_adapter.cpp                                               */
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
 * @file sushiruntime_adapter.cpp
 * @brief Implements the host's diffusion backend on SushiRuntime and maps its errors.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include "diffusion_backend.hpp"

#include <cstddef>
#include <utility>
#include <vector>

#include <SushiRuntime/sushiruntime.hpp>

namespace HostProgram
{
    /** @brief Holds the runtime, the field and the graph that steps the field. */
    struct DiffusionBackend final
    {
        /** @brief Takes a created runtime and starts an empty graph on it. */
        explicit DiffusionBackend(SushiRuntime::Runtime created)
            : runtime(std::move(created)), graph(runtime.graph())
        {

        }

        SushiRuntime::Runtime runtime;    /**< The runtime every handle below came from. */
        SushiRuntime::State<float> field; /**< The field; empty until allocate_field(). */
        SushiRuntime::Graph graph;        /**< Holds the one rule that steps the field. */
    };

    namespace
    {
        /** @brief Returns the host's status for an error of the library. */
        Status status_of(const SushiRuntime::Error& error)
        {
            using SushiRuntime::ErrorCode;
            switch (error.code)
            {
                case ErrorCode::AllocationFailed:
                    return Status{StatusCode::OutOfMemory, error.message};
                case ErrorCode::KernelFailed:
                case ErrorCode::DeviceLost:
                    return Status{StatusCode::DeviceFailed, error.message};
                case ErrorCode::InvalidGraph:
                case ErrorCode::InvalidAccess:
                case ErrorCode::InvalidState:
                case ErrorCode::InvalidArgument:
                    return Status{StatusCode::InvalidRequest, error.message};
                case ErrorCode::Unsupported:
                    return Status{StatusCode::Unsupported, error.message};
                case ErrorCode::CompileFailed:
                case ErrorCode::DanglingResource:
                case ErrorCode::ProtocolMismatch:
                    return Status{StatusCode::BackendFault, error.message};
            }
            return Status{StatusCode::BackendFault, error.message};
        }

        /** @brief Returns Ok for a result that holds a value, and the mapped error otherwise. */
        template <typename T>
        Status status_of(const SushiRuntime::Result<T>& result)
        {
            return result ? Status() : status_of(result.error());
        }
    } // namespace

    void BackendDeleter::operator()(DiffusionBackend* backend) const noexcept
    {
        delete backend;
    }

    Status create_backend(BackendHandle& backend)
    {
        // Runtime::create() stops the process on a library of another release.
        if (!SushiRuntime::version_matches())
            return Status{StatusCode::BackendFault,
                          "the loaded SushiRuntime is of another release than its headers"};

        SushiRuntime::Result<SushiRuntime::Runtime> created = SushiRuntime::Runtime::create();
        if (!created)
            return status_of(created.error());

        backend.reset(new DiffusionBackend(std::move(created).value()));
        return Status();
    }

    Status allocate_field(DiffusionBackend& backend, const std::vector<float>& initial, float rate)
    {
        const std::size_t cells = initial.size();
        if (cells == 0)
            return Status{StatusCode::InvalidRequest, "allocate_field: a field needs a cell"};

        SushiRuntime::Result<SushiRuntime::State<float>> allocated =
            backend.runtime.state<float>(cells);
        if (!allocated)
            return status_of(allocated.error());

        // The graph is cleared before the field is replaced, so no rule names the old one.
        backend.graph.clear();
        backend.field = std::move(allocated).value();
        backend.graph.add_rule(backend.field,
            [cells, rate](std::size_t i, const float* current, float* next)
            {
                const float left = (i == 0) ? current[i] : current[i - 1];
                const float right = (i == cells - 1) ? current[i] : current[i + 1];
                next[i] = current[i] + rate * (left - 2.0f * current[i] + right);
            });
        if (const SushiRuntime::Error* refusal = backend.graph.refusal())
            return status_of(*refusal);
        return status_of(backend.field.write_range({0, cells}, initial));
    }

    Status advance(DiffusionBackend& backend, std::size_t steps)
    {
        return status_of(backend.graph.run(steps));
    }

    Status read_field(const DiffusionBackend& backend, std::vector<float>& values)
    {
        SushiRuntime::Result<std::vector<float>> read =
            backend.field.read_range({0, backend.field.size()});
        if (!read)
            return status_of(read.error());

        values = std::move(read).value();
        return Status();
    }
} // namespace HostProgram
