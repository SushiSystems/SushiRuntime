/**************************************************************************/
/* bindless_detection.hpp                                                 */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
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
