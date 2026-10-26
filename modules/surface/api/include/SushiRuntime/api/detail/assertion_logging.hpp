/**************************************************************************/
/* assertion_logging.hpp                                                  */
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

/**
 * @file assertion_logging.hpp
 * @brief Declares install_fatal_logging, which puts a failed check into SushiLog at FATAL.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Installs an assertion handler that logs the failed check at FATAL, then calls
         *        the handler that was installed before it.
         *
         * Runtime::create calls it. The first call installs the handler and later calls do
         * nothing, so the handler that was current at the first call stays the one that runs
         * after the log record.
         */
        SUSHIRUNTIME_API void install_fatal_logging() noexcept;
    } // namespace Detail
} // namespace SushiRuntime
