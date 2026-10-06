/**************************************************************************/
/* sycl_compat.hpp                                                        */
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
