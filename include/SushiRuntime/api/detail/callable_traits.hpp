/**************************************************************************/
/* callable_traits.hpp                                                    */
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
 * @file callable_traits.hpp
 * @brief Declares the callable traits and pointer-unpacking helpers behind the add() overloads.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/resource_region.hpp>

namespace SushiRuntime
{
    namespace API
    {
        // Exactly one of the traits below enables each add() overload.

        /**
         * @brief A function callable once per element index: void(size_t).
         *
         * Submitted as a SYCL parallel_for over [0, n); the runtime picks the
         * device and the work-group mapping. Excludes the range-kernel shape so the
         * two add(size_t, F) overloads stay mutually exclusive.
         */
        template <class F>
        inline constexpr bool is_per_element_v =
            std::is_invocable_v<F, std::size_t> &&
            !std::is_invocable_v<F, std::size_t, std::size_t, sycl::handler&>;

        /**
         * @brief A raw kernel that drives a sycl::handler directly: void(sycl::handler&).
         */
        template <class F>
        inline constexpr bool is_raw_kernel_v =
            std::is_invocable_v<F, sycl::handler&>;

        /**
         * @brief A range kernel that receives its bounds and a handler:
         *        void(size_t begin, size_t end, sycl::handler&).
         */
        template <class F>
        inline constexpr bool is_range_kernel_v =
            std::is_invocable_v<F, std::size_t, std::size_t, sycl::handler&>;

        namespace detail
        {
            /**
             * @brief Calls fn(idx, ptr0, ptr1, ...) by unpacking the captured pointer tuple;
             *        device-safe.
             */
            template <class Fn, int N, class Tuple, std::size_t... Is>
            inline void invoke_with_pointers(const Fn& fn, sycl::id<N> idx,
                                             const Tuple& ptrs, std::index_sequence<Is...>)
            {
                fn(idx, std::get<Is>(ptrs)...);
            }

            /**
             * @brief Reads each coupled-input view's current pointer into a tuple, host-side at
             *        submit time.
             * @see include/SushiRuntime/api/detail/README.md
             */
            template <class Views, std::size_t... Is>
            inline auto views_current(const Views& views, std::index_sequence<Is...>)
            {
                return std::make_tuple(std::get<Is>(views)->cur...);
            }

            /**
             * @brief Calls fn(i, cur, next, in0, in1, ...) by unpacking the read-input pointer
             *        tuple.
             */
            template <class Fn, class T, class Tuple, std::size_t... Is>
            inline void invoke_state(const Fn& fn, std::size_t i,
                                     const T* cur, T* next,
                                     const Tuple& ins, std::index_sequence<Is...>)
            {
                fn(i, cur, next, std::get<Is>(ins)...);
            }

            /**
             * @brief Gathers the read keys of a tuple of State pointers, in order.
             * @see include/SushiRuntime/api/detail/README.md
             */
            template <class StatesTuple, std::size_t... Is>
            inline std::vector<Core::ResourceRegion> read_keys_of(const StatesTuple& states,
                                                                  std::index_sequence<Is...>)
            {
                return std::vector<Core::ResourceRegion>{ std::get<Is>(states)->read_key()... };
            }
        } // namespace detail

    } // namespace API
} // namespace SushiRuntime
