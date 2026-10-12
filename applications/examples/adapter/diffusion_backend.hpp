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

/** @brief Holds the host program of the adapter example; it names no type of the library. */
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

    /** @brief Creates a backend on the devices of this machine. */
    [[nodiscard]] Status create_backend(BackendHandle& backend);

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
