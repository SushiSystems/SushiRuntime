/**************************************************************************/
/* diffusion_backend.hpp                                                  */
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
 * @file diffusion_backend.hpp
 * @brief Declares the diffusion backend of the host program, in the host's own types.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace SushiRuntime
{
    class Runtime;
} // namespace SushiRuntime

/**
 * @brief Holds the host program of the adapter example; it names no type of the library but the
 *        runtime a host that already has one passes to create_backend().
 */
namespace HostProgram
{
    /** @brief Names the outcome of a backend call in the host's own terms. */
    enum class StatusCode : std::uint8_t
    {
        Ok,             /**< The call did what it was asked. */
        OutOfMemory,    /**< The backend could not allocate what the call needs. */
        DeviceFailed,   /**< A device faulted or was lost; the backend is to be rebuilt. */
        InvalidRequest, /**< The host asked for something the backend cannot do as asked. */
        Unsupported,    /**< The backend lacks the capability on this machine. */
        BackendFault    /**< The backend failed in a way the host cannot act on. */
    };

    /** @brief Returns the enumerator's spelling. */
    [[nodiscard]] inline const char* to_string(StatusCode code) noexcept
    {
        switch (code)
        {
            case StatusCode::Ok:             return "Ok";
            case StatusCode::OutOfMemory:    return "OutOfMemory";
            case StatusCode::DeviceFailed:   return "DeviceFailed";
            case StatusCode::InvalidRequest: return "InvalidRequest";
            case StatusCode::Unsupported:    return "Unsupported";
            case StatusCode::BackendFault:   return "BackendFault";
        }
        return "Unknown";
    }

    /** @brief Holds the outcome of a backend call and the sentence that describes a failure. */
    struct Status
    {
        StatusCode code = StatusCode::Ok; /**< The outcome. */
        std::string message;              /**< What failed; empty on success. */

        /** @brief Returns whether the call succeeded. */
        [[nodiscard]] bool is_ok() const noexcept { return code == StatusCode::Ok; }
    };

    /** @brief Stands for the backend; the adapter alone defines it. */
    struct DiffusionBackend;

    /** @brief Destroys a backend through the adapter, which alone knows its members. */
    struct BackendDeleter
    {
        /** @brief Destroys the backend; null is accepted. */
        void operator()(DiffusionBackend* backend) const noexcept;
    };

    /** @brief Owns a backend the host cannot look into. */
    using BackendHandle = std::unique_ptr<DiffusionBackend, BackendDeleter>;

    /** @brief Creates a backend on a runtime of its own, over the devices of this machine. */
    [[nodiscard]] Status create_backend(BackendHandle& backend);

    /**
     * @brief Creates a backend on a runtime the host already holds.
     * @param runtime The runtime the backend runs on; it must outlive the backend.
     */
    [[nodiscard]] Status create_backend(BackendHandle& backend, SushiRuntime::Runtime& runtime);

    /**
     * @brief Returns the value of cell @p i after one step: it moves toward its neighbours by
     *        @p rate of the difference, and an edge cell counts itself as its missing neighbour.
     * @param i       The cell; below @p cells.
     * @param cells   The cell count of the field.
     * @param rate    Share of the difference to the neighbours one step moves.
     * @param current The field before the step.
     */
    [[nodiscard]] inline float diffused_cell(std::size_t i, std::size_t cells, float rate,
                                             const float* current) noexcept
    {
        const float left = (i == 0) ? current[i] : current[i - 1];
        const float right = (i == cells - 1) ? current[i] : current[i + 1];
        return current[i] + rate * (left - 2.0f * current[i] + right);
    }

    /**
     * @brief Advances @p field by one step on the host, the step each device launch runs.
     * @param field The field; replaced by its value after the step.
     * @param rate  Share of the difference to the neighbours one step moves.
     */
    inline void diffuse_on_host(std::vector<float>& field, float rate)
    {
        const std::vector<float> current = field;
        for (std::size_t i = 0; i < field.size(); ++i)
            field[i] = diffused_cell(i, field.size(), rate, current.data());
    }

    /**
     * @brief Allocates a field that each step moves toward its neighbours, and seeds it.
     * @param initial One value per cell; must hold at least one.
     * @param rate    Share of the difference to the neighbours one step moves; at most 0.5.
     */
    [[nodiscard]] Status allocate_field(DiffusionBackend& backend,
                                        const std::vector<float>& initial, float rate);

    /** @brief Advances the field by a number of steps. */
    [[nodiscard]] Status advance(DiffusionBackend& backend, std::size_t steps);

    /** @brief Reads the whole field into values, which is resized to the cell count. */
    [[nodiscard]] Status read_field(const DiffusionBackend& backend, std::vector<float>& values);
} // namespace HostProgram
