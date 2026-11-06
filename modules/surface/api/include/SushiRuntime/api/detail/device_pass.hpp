/**************************************************************************/
/* device_pass.hpp                                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file device_pass.hpp
 * @brief Declares require_device_pass(), which every kernel template of the API calls first.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <type_traits>

#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Tells whether this unit compiles Kernel for the device: false only where the
         *        DPC++ headers are compiled without -fsycl.
         * @tparam Kernel The kernel being launched; it makes the value depend on the launch.
         */
        template <class Kernel>
        inline constexpr bool compiled_for_device_v =
#if defined(__SYCL_COMPILER_VERSION) && !defined(SYCL_LANGUAGE_VERSION)
            !std::is_same_v<Kernel, Kernel>;
#else
            std::is_same_v<Kernel, Kernel>;
#endif

        /**
         * @brief Stops the build of a unit that launches Kernel without compiling it for the
         *        device.
         * @tparam Kernel The kernel being launched.
         */
        template <class Kernel>
        constexpr void require_device_pass() noexcept
        {
            static_assert(compiled_for_device_v<Kernel>,
                          "SushiRuntime: this translation unit launches a kernel and is compiled "
                          "without -fsycl. Compile it with -fsycl; in SushiRuntime's own build, "
                          "list it in DEVICE_SOURCES.");
        }
    } // namespace Detail
} // namespace SushiRuntime
