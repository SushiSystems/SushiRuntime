/**************************************************************************/
/* callable_traits.hpp                                                    */
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
        // The three callable shapes the fluent add() overloads accept, expressed
        // as C++17 SFINAE traits over std::is_invocable_v (the C++20 concepts they
        // replace read the same, only as `requires`-clauses). enable_if on each
        // add() selects exactly one. A wrong signature removes every overload, so
        // the compiler reports "no matching call" — less pointed than a concept
        // diagnostic but still naming the offending callable.

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
            // Calls fn(idx, ptr0, ptr1, ...) by unpacking the captured pointer
            // tuple. Kept free so it is usable from inside a device lambda; all
            // operations (std::get over a tuple of pointers) are device-safe.
            template <class Fn, int N, class Tuple, std::size_t... Is>
            inline void invoke_with_pointers(const Fn& fn, sycl::id<N> idx,
                                             const Tuple& ptrs, std::index_sequence<Is...>)
            {
                fn(idx, std::get<Is>(ptrs)...);
            }

            // Reads each coupled-input view's current pointer into a plain tuple,
            // host-side at submit time. A StateView holds the live current/next
            // pointers for the step, so dereferencing here (not in the kernel) lets
            // the device lambda capture raw pointers and still follow the swap.
            template <class Views, std::size_t... Is>
            inline auto views_current(const Views& views, std::index_sequence<Is...>)
            {
                return std::make_tuple(std::get<Is>(views)->cur...);
            }

            // Calls fn(i, cur, next, in0, in1, ...) by unpacking the read-input
            // pointer tuple. Device-safe: every tuple element is a raw pointer.
            template <class Fn, class T, class Tuple, std::size_t... Is>
            inline void invoke_state(const Fn& fn, std::size_t i,
                                     const T* cur, T* next,
                                     const Tuple& ins, std::index_sequence<Is...>)
            {
                fn(i, cur, next, std::get<Is>(ins)...);
            }

            // Gathers the read keys of a tuple of State pointers, in order. Called
            // at compile time so the dependency tracker labels every coupled input
            // consistently with the canonical single-State overload.
            template <class StatesTuple, std::size_t... Is>
            inline std::vector<Core::ResourceRegion> read_keys_of(const StatesTuple& states,
                                                                  std::index_sequence<Is...>)
            {
                return std::vector<Core::ResourceRegion>{ std::get<Is>(states)->read_key()... };
            }
        } // namespace detail

    } // namespace API
} // namespace SushiRuntime
