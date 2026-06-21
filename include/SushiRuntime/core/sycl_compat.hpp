/**************************************************************************/
/* sycl_compat.hpp                                                        */
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

#pragma once

#include <type_traits>
#include <utility>
#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace detail
    {
        /**
         * @brief Detects whether a SYCL command group handler exposes host_task.
         *
         * Uses SFINAE to verify if the handler supports the standard host_task method at compile time.
         * 
         * @tparam Handler The type of the SYCL command group handler.
         * @tparam Fn The type of the callable task.
         * @tparam Unnamed SFINAE helper parameter.
         */
        template <class Handler, class Fn, class = void>
        struct has_host_task : std::false_type {};

        template <class Handler, class Fn>
        struct has_host_task<Handler, Fn,
            std::void_t<decltype(std::declval<Handler&>().host_task(std::declval<Fn>()))>>
            : std::true_type {};

        template <class Handler, class Fn>
        inline constexpr bool has_host_task_v = has_host_task<Handler, Fn>::value;

        /**
         * @brief Enqueues a host-side callable into a SYCL command group.
         *
         * Selects the appropriate host execution mechanism dynamically based on the toolchain capabilities.
         *
         * @tparam Handler The type of the SYCL command group handler.
         * @tparam Fn The type of the callable host task.
         * @param h The active command-group handler managing the execution queue.
         * @param fn The host callable to execute asynchronously, expecting no arguments.
         */
        template <class Handler, class Fn>
        inline void enqueue_host_task(Handler& h, Fn&& fn)
        {
            if constexpr (has_host_task_v<Handler, Fn>)
            {
                h.host_task(std::forward<Fn>(fn));
            }
            else
            {
                // AdaptiveCpp path: the custom operation runs on the host once its
                // dependencies are satisfied. The interop_handle argument is
                // unused; accept it generically so its type need not be named.
                h.AdaptiveCpp_enqueue_custom_operation(
                    [fn = std::forward<Fn>(fn)](auto&&) mutable { fn(); });
            }
        }

    } // namespace detail
} // namespace SushiRuntime
