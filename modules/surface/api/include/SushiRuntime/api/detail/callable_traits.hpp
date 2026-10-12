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
 * @brief Declares the callable traits and the helpers that unpack the States of a coupled rule.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Tells whether a const F is callable as kernel(std::size_t) and is not a
         *        range kernel.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class F>
        inline constexpr bool is_per_element_v =
            std::is_invocable_v<const std::decay_t<F>&, std::size_t> &&
            !std::is_invocable_v<const std::decay_t<F>&, std::size_t, std::size_t,
                                 sycl::handler&>;

        /**
         * @brief Tells whether F is callable as kernel(sycl::handler&), the raw kernel shape.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class F>
        inline constexpr bool is_raw_kernel_v =
            std::is_invocable_v<F, sycl::handler&>;

        /**
         * @brief Tells whether a const F is callable as kernel(std::size_t begin, std::size_t end,
         *        sycl::handler&), the range kernel shape.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class F>
        inline constexpr bool is_range_kernel_v =
            std::is_invocable_v<const std::decay_t<F>&, std::size_t, std::size_t,
                                sycl::handler&>;

        /**
         * @brief Tells whether F is callable as kernel(sycl::queue&,
         *        const std::vector<sycl::event>&) and returns a sycl::event, the task body shape.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class F>
        inline constexpr bool is_task_body_v =
            std::is_invocable_r_v<sycl::event, std::decay_t<F>&, sycl::queue&,
                                  const std::vector<sycl::event>&>;

        /** @brief Returns a field pointer as the read-only pointer a kernel input receives. */
        template <typename T>
        constexpr const T* read_only(T* field) noexcept
        {
            return field;
        }

        /**
         * @brief Reads each coupled-input view's current field into a tuple of const
         *        pointers, on the host when the node is submitted.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class Views, std::size_t... Is>
        inline auto views_current(const Views& views, std::index_sequence<Is...>)
        {
            return std::make_tuple(read_only(std::get<Is>(views)->current)...);
        }

        /**
         * @brief Gathers the current field of each view as a read region, in order.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class Views, std::size_t... Is>
        inline std::vector<BufferRegion> read_keys_of_views(
            const Views& views, std::index_sequence<Is...>)
        {
            return std::vector<BufferRegion>{
                BufferRegion(static_cast<void*>(std::get<Is>(views)->current))... };
        }
    } // namespace Detail
} // namespace SushiRuntime
