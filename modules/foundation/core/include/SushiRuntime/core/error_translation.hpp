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
 * @brief Defines translate_exceptions, which returns what a call throws as an Error.
 * @author Mustafa Garip
 */

#pragma once

#include <exception>
#include <new>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief The message of a thrown object that does not derive from std::exception. */
        inline constexpr const char* K_UNNAMED_EXCEPTION_MESSAGE =
            "An object that does not derive from std::exception was thrown.";

        /**
         * @brief Calls function and returns what it throws as an Error.
         * @param code Code of a std::exception other than std::bad_alloc, and of an object
         *        that is no std::exception.
         * @return Nothing when function returned; an AllocationFailed error for
         *         std::bad_alloc; else an error of code, whose message is empty when the
         *         text could not be allocated.
         */
        template <class Function>
        [[nodiscard]] Result<void> translate_exceptions(ErrorCode code,
                                                        Function&& function) noexcept
        {
            try
            {
                std::forward<Function>(function)();
                return {};
            }
            catch (const std::bad_alloc& failure)
            {
                return make_error(
                    error_without_throwing(ErrorCode::AllocationFailed, failure.what()));
            }
            catch (const std::exception& failure)
            {
                return make_error(error_without_throwing(code, failure.what()));
            }
            catch (...)
            {
                return make_error(error_without_throwing(code, K_UNNAMED_EXCEPTION_MESSAGE));
            }
        }

        /**
         * @brief Calls function and returns what it throws as an Error.
         * @return Nothing when function returned; an AllocationFailed error for
         *         std::bad_alloc; else a KernelFailed error.
         */
        template <class Function>
        [[nodiscard]] Result<void> translate_exceptions(Function&& function) noexcept
        {
            return translate_exceptions(ErrorCode::KernelFailed,
                                        std::forward<Function>(function));
        }
    } // namespace Detail
} // namespace SushiRuntime
