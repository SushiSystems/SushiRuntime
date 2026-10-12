/**************************************************************************/
/* error.hpp                                                              */
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
 * @file error.hpp
 * @brief Declares ErrorCode and Error, the runtime's failure types.
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    /**
     * @brief Names the class of a failure the runtime reports.
     * @see modules/foundation/core/README.md
     */
    enum class ErrorCode : std::uint8_t
    {
        InvalidGraph = 1, /**< The graph is empty, cyclic, or otherwise unrunnable. */
        CompileFailed,    /**< Dependency analysis or graph compilation failed. */
        AllocationFailed, /**< A USM or host allocation could not be satisfied. */
        KernelFailed,     /**< A kernel faulted on the device (async channel). */
        DeviceLost,       /**< The target device disappeared or became unusable. */
        InvalidAccess,    /**< A handle was accessed in a way its residency forbids. */
        DanglingResource, /**< A run referenced a handle whose allocation was freed. */
        Unsupported,      /**< The implementation does not offer the requested capability. */
        InvalidState,     /**< The call is not allowed in the object's current state. */
        ProtocolMismatch  /**< A peer speaks another version of the wire protocol. */
    };

    /**
     * @brief Returns the enumerator's spelling, or "Unknown" for a value outside the enumeration.
     */
    [[nodiscard]] inline const char* to_string(ErrorCode code) noexcept
    {
        switch (code)
        {
            case ErrorCode::InvalidGraph:     return "InvalidGraph";
            case ErrorCode::CompileFailed:    return "CompileFailed";
            case ErrorCode::AllocationFailed: return "AllocationFailed";
            case ErrorCode::KernelFailed:     return "KernelFailed";
            case ErrorCode::DeviceLost:       return "DeviceLost";
            case ErrorCode::InvalidAccess:    return "InvalidAccess";
            case ErrorCode::DanglingResource: return "DanglingResource";
            case ErrorCode::Unsupported:      return "Unsupported";
            case ErrorCode::InvalidState:     return "InvalidState";
            case ErrorCode::ProtocolMismatch: return "ProtocolMismatch";
        }
        return "Unknown";
    }

    /**
     * @brief Holds the class of a failure and the sentence that describes it.
     */
    struct Error
    {
        /**
         * @brief Builds the failure of one class with the sentence that describes it.
         */
        Error(ErrorCode failure_code, std::string failure_message) noexcept
            : code(failure_code), message(std::move(failure_message))
        {
        }

        ErrorCode code;      /**< The class of the failure. */
        std::string message; /**< What failed, in one sentence. */
    };

    /**
     * @brief Holds what the library's own code builds an Error with.
     */
    namespace Detail
    {
        /**
         * @brief Returns an Error built without throwing.
         * @param message Text of the Error; null-terminated.
         * @return The Error, with an empty message when the text could not be allocated.
         */
        [[nodiscard]] inline Error error_without_throwing(ErrorCode code,
                                                          const char* message) noexcept
        {
            try
            {
                return Error{code, message};
            }
            catch (...)
            {
                return Error{code, std::string()};
            }
        }

        /**
         * @brief Returns a copy of an Error made without throwing.
         * @return The copy, with an empty message when the text could not be allocated.
         */
        [[nodiscard]] inline Error error_without_throwing(const Error& source) noexcept
        {
            try
            {
                return source;
            }
            catch (...)
            {
                return Error{source.code, std::string()};
            }
        }
    } // namespace Detail

} // namespace SushiRuntime
