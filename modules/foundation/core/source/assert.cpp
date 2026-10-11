/**************************************************************************/
/* assert.cpp                                                             */
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
 * @file assert.cpp
 * @brief Holds the assertion handler and stops the process after calling it.
 * @author Mustafa Garip
 */

#include <SushiRuntime/core/assert.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Writes the failure to standard error, on one line. */
        void write_failure_to_standard_error(const AssertionFailure& failure)
        {
            std::fprintf(stderr, "SushiRuntime: %s failed: %s (%s:%d)\n",
                         failure.expression != nullptr ? failure.expression : "fatal",
                         failure.message != nullptr ? failure.message : "",
                         failure.file != nullptr ? failure.file : "",
                         failure.line);
            std::fflush(stderr);
        }

        /// @brief The handler a failed check calls.
        std::atomic<AssertionHandler> installed_handler{&write_failure_to_standard_error};
    } // namespace

    AssertionHandler set_assertion_handler(AssertionHandler handler) noexcept
    {
        if (handler == nullptr)
            handler = &write_failure_to_standard_error;
        return installed_handler.exchange(handler, std::memory_order_acq_rel);
    }

    void fail_assertion(const AssertionFailure& failure) noexcept
    {
        installed_handler.load(std::memory_order_acquire)(failure);
        std::abort();
    }

} // namespace SushiRuntime
