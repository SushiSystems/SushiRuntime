/**************************************************************************/
/* errors.hpp                                                             */
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
 * @file errors.hpp
 * @brief Promotes the error and result types a run returns into SushiRuntime.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#pragma once

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    using Core::Errc;
    using Core::Error;
    using Core::Result;
    using Core::SushiException;

} // namespace SushiRuntime
