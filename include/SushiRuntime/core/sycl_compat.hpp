/****************************************************************/
/* sycl_compat.hpp                                              */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file sycl_compat.hpp
 * @brief Declares helpers that hide host-task differences between SYCL toolchains.
 * @author Mustafa Garip
 * @see include/SushiRuntime/core/README.md
 */

#pragma once

#include <type_traits>
#include <utility>
#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace detail
    {
        /** @brief Detects whether a SYCL command group handler exposes host_task. */
        template <class Handler, class Fn, class = void>
        struct has_host_task : std::false_type {};

        template <class Handler, class Fn>
        struct has_host_task<Handler, Fn,
            std::void_t<decltype(std::declval<Handler&>().host_task(std::declval<Fn>()))>>
            : std::true_type {};

        template <class Handler, class Fn>
        inline constexpr bool has_host_task_v = has_host_task<Handler, Fn>::value;

        /** @brief Enqueues a host-side callable through whichever mechanism the toolchain has. */
        template <class Handler, class Fn>
        inline void enqueue_host_task(Handler& h, Fn&& fn)
        {
            if constexpr (has_host_task_v<Handler, Fn>)
            {
                h.host_task(std::forward<Fn>(fn));
            }
            else
            {
                // AdaptiveCpp path: the interop_handle argument is unused and accepted generically.
                h.AdaptiveCpp_enqueue_custom_operation(
                    [fn = std::forward<Fn>(fn)](auto&&) mutable { fn(); });
            }
        }

    } // namespace detail
} // namespace SushiRuntime
