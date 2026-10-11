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
 * @brief Declares Errc, Error and SushiException, the runtime's failure types.
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
     * @brief Categorizes every failure the runtime can report.
     *
     * Provides a distinct code per failure class so callers can handle specific errors programmatically.
     */
    enum class Errc : std::uint8_t
    {
        ok,                 /**< No error. */
        invalid_graph,      /**< The graph is empty, cyclic, or otherwise unrunnable. */
        compile_failed,     /**< Dependency analysis or graph compilation failed. */
        allocation_failed,  /**< A USM or host allocation could not be satisfied. */
        kernel_failed,      /**< A kernel faulted on the device (async channel). */
        device_lost,        /**< The target device disappeared or became unusable. */
        invalid_access,     /**< A handle was accessed in a way its residency forbids. */
        dangling_resource,  /**< A run referenced a handle whose allocation was freed. */
        unsupported,        /**< The implementation does not offer the requested capability. */
        invalid_state       /**< The call is not allowed in the object's current state. */
    };

    /**
     * @brief Maps an error code to a short, stable English label.
     *
     * Returns a static string literal corresponding to the error code for logging and formatting.
     *
     * @param code The specific error code to translate.
     * @return A null-terminated static string literal representing the error name.
     */
    [[nodiscard]] inline const char* to_string(Errc code) noexcept
    {
        switch (code)
        {
            case Errc::ok:                return "ok";
            case Errc::invalid_graph:     return "invalid_graph";
            case Errc::compile_failed:    return "compile_failed";
            case Errc::allocation_failed: return "allocation_failed";
            case Errc::kernel_failed:     return "kernel_failed";
            case Errc::device_lost:       return "device_lost";
            case Errc::invalid_access:    return "invalid_access";
            case Errc::dangling_resource: return "dangling_resource";
            case Errc::unsupported:       return "unsupported";
            case Errc::invalid_state:     return "invalid_state";
        }
        return "unknown";
    }

    /**
     * @brief Represents a structured runtime failure containing a category and a diagnostic message.
     *
     * Carries the error context for non-throwing return channels.
     */
    struct Error
    {
        Errc code = Errc::ok;   /**< The failure category. */
        std::string message;    /**< The human-readable diagnostic detail. */
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
             * @param err The structured failure detail to carry.
             */
            explicit SushiException(Error err)
                : std::runtime_error(err.message), err_(std::move(err))
            {
            }

            /** 
             * @brief Retrieves the structured error behind this exception. 
             * 
             * Exposes the underlying Error object for programmatic inspection.
             *
             * @return A constant reference to the underlying structured error.
             */
            [[nodiscard]] const Error& error() const noexcept { return err_; }

        private:
            Error err_; /**< The structured failure detail. */
    };

    // RunReport, the success value of try_run()'s Result, lives in observability/run_report.hpp.
} // namespace SushiRuntime
