/**************************************************************************/
/* error_translation.hpp                                                  */
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
 * @file error_translation.hpp
 * @brief Defines the translation of what SYCL code throws into an Error.
 * @author Mustafa Garip
 * @see modules/foundation/sycl/README.md
 */

#pragma once

#include <optional>
#include <utility>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Internal::SYCL
    {
        /**
         * @brief Returns the code of a SYCL failure.
         * @param device_code Code of a failure whose sycl::errc names no refused memory.
         * @return AllocationFailed for sycl::errc::memory_allocation, else device_code.
         */
        [[nodiscard]] inline ErrorCode error_code_of(const sycl::exception& failure,
                                                     ErrorCode device_code) noexcept
        {
            return failure.code() == sycl::make_error_code(sycl::errc::memory_allocation)
                ? ErrorCode::AllocationFailed : device_code;
        }

        /**
         * @brief Calls function and returns what it throws as an Error.
         * @param device_code Code of a sycl::exception, through error_code_of().
         * @param code Code of any other failure but std::bad_alloc; see
         *        Detail::translate_exceptions().
         * @return Nothing when function returned, else the error.
         */
        template <class Function>
        [[nodiscard]] Result<void> translate_exceptions(ErrorCode device_code, ErrorCode code,
                                                        Function&& function) noexcept
        {
            std::optional<Error> device_failure;
            Result<void> translated = Detail::translate_exceptions(
                code, [&function, &device_failure, device_code]()
            {
                try
                {
                    std::forward<Function>(function)();
                }
                catch (const sycl::exception& failure)
                {
                    device_failure.emplace(Detail::error_without_throwing(
                        error_code_of(failure, device_code), failure.what()));
                }
            });
            if (device_failure)
                return make_error(std::move(*device_failure));
            return translated;
        }

        /**
         * @brief Calls function and returns what it throws as an Error.
         * @return Nothing when function returned; an AllocationFailed error for refused
         *         memory; else a KernelFailed error.
         */
        template <class Function>
        [[nodiscard]] Result<void> translate_exceptions(Function&& function) noexcept
        {
            return translate_exceptions(ErrorCode::KernelFailed, ErrorCode::KernelFailed,
                                        std::forward<Function>(function));
        }
    } // namespace Internal::SYCL
} // namespace SushiRuntime
