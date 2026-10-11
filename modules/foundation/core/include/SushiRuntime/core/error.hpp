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
 * @brief Declares ErrorCode, Error and SushiException, the runtime's failure types.
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <stdexcept>
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
        Ok,               /**< No error. */
        InvalidGraph,     /**< The graph is empty, cyclic, or otherwise unrunnable. */
        CompileFailed,    /**< Dependency analysis or graph compilation failed. */
        AllocationFailed, /**< A USM or host allocation could not be satisfied. */
        KernelFailed,     /**< A kernel faulted on the device (async channel). */
        DeviceLost,       /**< The target device disappeared or became unusable. */
        InvalidAccess,    /**< A handle was accessed in a way its residency forbids. */
        DanglingResource, /**< A run referenced a handle whose allocation was freed. */
        Unsupported,      /**< The implementation does not offer the requested capability. */
        InvalidState      /**< The call is not allowed in the object's current state. */
    };

    /**
     * @brief Returns the enumerator's spelling, or "Unknown" for a value outside the enumeration.
     */
    [[nodiscard]] inline const char* to_string(ErrorCode code) noexcept
    {
        switch (code)
        {
            case ErrorCode::Ok:               return "Ok";
            case ErrorCode::InvalidGraph:     return "InvalidGraph";
            case ErrorCode::CompileFailed:    return "CompileFailed";
            case ErrorCode::AllocationFailed: return "AllocationFailed";
            case ErrorCode::KernelFailed:     return "KernelFailed";
            case ErrorCode::DeviceLost:       return "DeviceLost";
            case ErrorCode::InvalidAccess:    return "InvalidAccess";
            case ErrorCode::DanglingResource: return "DanglingResource";
            case ErrorCode::Unsupported:      return "Unsupported";
            case ErrorCode::InvalidState:     return "InvalidState";
        }
        return "Unknown";
    }

    /**
     * @brief Holds the class of a failure and the sentence that describes it.
     */
    struct Error
    {
        ErrorCode code = ErrorCode::Ok; /**< The class of the failure. */
        std::string message;            /**< What failed, in one sentence. */
    };

    /**
     * @brief Exception form of Error, thrown by the throwing entry points.
     *
     * Wraps an Error object and exposes its message through the std::runtime_error interface.
     */
    class SR_TYPE_API SushiException : public std::runtime_error
    {
        public:
            /**
             * @brief Wraps an Error as a throwable exception.
             * 
             * Initializes the standard exception with the error message.
             *
             * @param failure The structured failure detail to carry.
             */
            explicit SushiException(Error failure)
                : std::runtime_error(failure.message), error_(std::move(failure))
            {
            }

            /** 
             * @brief Retrieves the structured error behind this exception. 
             * 
             * Exposes the underlying Error object for programmatic inspection.
             *
             * @return A constant reference to the underlying structured error.
             */
            [[nodiscard]] const Error& error() const noexcept { return error_; }

        private:
            Error error_; /**< The structured failure detail. */
    };

} // namespace SushiRuntime
