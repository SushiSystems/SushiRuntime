/****************************************************************/
/* bindless_detection.hpp                                       */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
