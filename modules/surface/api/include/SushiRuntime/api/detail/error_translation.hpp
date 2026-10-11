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
 * @brief Declares translate_exceptions, which turns what a call throws into an Error.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <exception>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Runs work and returns what it throws as an Error.
         * @param device_code Code of a sycl::exception.
         * @param fallback    Code of any other exception that carries none.
         * @return An Error whose code is Ok when work returned; its message is empty when
         *         the text could not be allocated.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class Work>
        [[nodiscard]] Error translate_exceptions(ErrorCode device_code, ErrorCode fallback,
                                                 Work&& work) noexcept
        {
            try
            {
                work();
                return Error{};
            }
            catch (const SushiException& failure)
            {
                return error_without_throwing(failure.error());
            }
            catch (const sycl::exception& failure)
            {
                return error_without_throwing(device_code, failure.what());
            }
            catch (const std::exception& failure)
            {
                return error_without_throwing(fallback, failure.what());
            }
            catch (...)
            {
                return error_without_throwing(
                    fallback, "An object that does not derive from std::exception was thrown.");
            }
        }
    } // namespace Detail
} // namespace SushiRuntime
