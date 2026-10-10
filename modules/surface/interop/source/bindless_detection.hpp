/**************************************************************************/
/* bindless_detection.hpp                                                 */
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
 * @file bindless_detection.hpp
 * @brief Detects toolchain support for the SYCL bindless images extension.
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#pragma once

#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Interop
    {
        namespace Detail
        {
#if defined(SYCL_EXT_ONEAPI_BINDLESS_IMAGES)
            inline constexpr bool BINDLESS_IMAGES_AVAILABLE = true;
#else
            inline constexpr bool BINDLESS_IMAGES_AVAILABLE = false;
#endif

        } // namespace Detail
    } // namespace Interop
} // namespace SushiRuntime
