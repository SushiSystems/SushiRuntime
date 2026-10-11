/**************************************************************************/
/* callable_checks.hpp                                                    */
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
 * @file callable_checks.hpp
 * @brief Declares the checks that reject a callable of the wrong signature at add().
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <type_traits>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/callable_traits.hpp>
#include <SushiRuntime/api/detail/emit_launch.hpp>
#include <SushiRuntime/api/launch/extent.hpp>
#include <SushiRuntime/api/launch/nd_range.hpp>
#include <SushiRuntime/api/vocabulary/captures.hpp>

namespace SushiRuntime
{
    class Recorder;

    namespace Detail
    {
        /**
         * @brief Tells whether a launch can call a kernel with its index and then Extra.
         * @tparam Launch std::size_t, Extent<N> or NdRange<N>; anything else is false.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class Launch, class F, class... Extra>
        inline constexpr bool is_launch_kernel_v = false;

        /** @brief Tells whether a count can call the kernel as kernel(std::size_t, extra...). */
        template <class F, class... Extra>
        inline constexpr bool is_launch_kernel_v<std::size_t, F, Extra...> =
            std::is_invocable_v<const std::decay_t<F>&, std::size_t, Extra...>;

        /** @brief Tells whether an Extent can call the kernel as kernel(sycl::id<N>, extra...). */
        template <int N, class F, class... Extra>
        inline constexpr bool is_launch_kernel_v<Extent<N>, F, Extra...> =
            std::is_invocable_v<const std::decay_t<F>&, sycl::id<N>, Extra...>;

        /**
         * @brief Tells whether an NdRange can call the kernel as kernel(item, extra...) or
         *        kernel(item, id, extra...).
         */
        template <int N, class F, class... Extra>
        inline constexpr bool is_launch_kernel_v<NdRange<N>, F, Extra...> =
            std::is_invocable_v<const std::decay_t<F>&, sycl::nd_item<N>, Extra...> ||
            std::is_invocable_v<const std::decay_t<F>&, sycl::nd_item<N>, sycl::id<N>,
                                Extra...>;

        /**
         * @brief Checks the kernel of a launch that hands over its index and nothing else.
         * @return Whether kernel fits; a false return follows a failed static_assert.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class Launch, class F>
        constexpr bool accepts_kernel() noexcept
        {
            constexpr bool accepted =
                is_launch_kernel_v<Launch, F> ||
                (std::is_same_v<Launch, std::size_t> && is_range_kernel_v<F>);
            static_assert(accepted,
                "add(launch, reads, writes, kernel) and add_untracked(launch, kernel) take "
                "kernel(index): index is std::size_t for a count, sycl::id<N> for an "
                "Extent<N>, and sycl::nd_item<N>, optionally followed by sycl::id<N>, for an "
                "NdRange<N>; a count also takes kernel(std::size_t begin, std::size_t end, "
                "sycl::handler&).");
            return accepted;
        }

        /**
         * @brief Checks the kernel of a launch that hands over one pointer per capture.
         * @tparam Pointers The pointer type of each In, Out or InOut, in argument order.
         */
        template <class Launch, class F, class... Pointers>
        constexpr bool accepts_captured_kernel() noexcept
        {
            constexpr bool accepted = is_launch_kernel_v<Launch, F, Pointers...>;
            static_assert(accepted,
                "add(launch, In/Out/InOut..., kernel) takes kernel(index, pointers...): one "
                "pointer per capture in argument order, const T* for In<T> and T* for Out<T> and "
                "InOut<T>, after the index of the launch (std::size_t for a count, "
                "sycl::id<N> for an Extent<N>, sycl::nd_item<N>, optionally followed by "
                "sycl::id<N>, for an NdRange<N>).");
            return accepted;
        }

        /**
         * @brief Checks the update rule of a State<T>.
         * @tparam Launch std::size_t for the whole field.
         */
        template <class Launch, typename T, class F>
        constexpr bool accepts_state_rule() noexcept
        {
            constexpr bool accepted = is_launch_kernel_v<Launch, F, const T*, T*>;
            static_assert(accepted,
                "add_rule(state, kernel) and add_rule(launch, state, kernel) take "
                "kernel(index, const T* current, T* next) for a State<T>: index is "
                "std::size_t for the whole field or a count, sycl::id<N> for an Extent<N>, "
                "and sycl::nd_item<N>, optionally followed by sycl::id<N>, for an NdRange<N>.");
            return accepted;
        }

        /**
         * @brief Checks the update rule of a Coupled set of States.
         * @tparam Out    Element type of the State written.
         * @tparam Inputs Element type of each State read, in argument order.
         */
        template <class Launch, class F, typename Out, typename... Inputs>
        constexpr bool accepts_coupled_rule() noexcept
        {
            constexpr bool accepted =
                is_launch_kernel_v<Launch, F, const Out*, Out*, const Inputs*...>;
            static_assert(accepted,
                "add_coupled(Coupled(out, in0, ...), kernel) and add_coupled(launch, "
                "Coupled(out, in0, ...), kernel) take kernel(index, const T* current, T* next, "
                "const A* in0, ...): the two fields of the State written, then the current "
                "field of each State read, in argument order, after the index of the launch.");
            return accepted;
        }

        /** @brief Checks a kernel that is called once per element index and nothing else. */
        template <class F>
        constexpr bool accepts_per_element() noexcept
        {
            constexpr bool accepted = is_per_element_v<F>;
            static_assert(accepted,
                "add_dynamic(Dynamic, capacity, reads, writes, kernel) and add(OperationID, n, "
                "reads, writes, local) take a callable invoked as kernel(std::size_t i) for "
                "each element index.");
            return accepted;
        }

        /** @brief Checks a raw kernel, which submits its own command group. */
        template <class F>
        constexpr bool accepts_raw_kernel() noexcept
        {
            constexpr bool accepted = is_raw_kernel_v<F>;
            static_assert(accepted,
                "add(reads, writes, kernel) and add_untracked(placement, kernel) take a raw "
                "kernel invoked as kernel(sycl::handler&), which submits its own command group; a "
                "per-element kernel needs a launch as the first argument.");
            return accepted;
        }

        /** @brief Checks a host task. */
        template <class F>
        constexpr bool accepts_host_task() noexcept
        {
            constexpr bool accepted = std::is_invocable_v<F>;
            static_assert(accepted,
                "add_host(reads, writes, body) and add_host(body) take a body invoked as "
                "body() with no argument; it runs on a worker thread and captures what it "
                "needs.");
            return accepted;
        }

        /** @brief Checks a task body, which submits its own work on the queue it is given. */
        template <class F>
        constexpr bool accepts_task() noexcept
        {
            constexpr bool accepted = is_task_body_v<F>;
            static_assert(accepted,
                "add_task(reads, writes, body) takes a body invoked as "
                "body(sycl::queue&, const std::vector<sycl::event>&) that returns the "
                "sycl::event which ends its work.");
            return accepted;
        }

        /** @brief Checks a hook a graph calls before each step. */
        template <class F>
        constexpr bool accepts_step_hook() noexcept
        {
            constexpr bool accepted = std::is_invocable_v<std::decay_t<F>&, std::size_t>;
            static_assert(accepted,
                "before_each_step(hook) takes a hook invoked as "
                "hook(std::size_t finished_steps) on the thread that runs the graph, before "
                "each step.");
            return accepted;
        }

        /** @brief Checks an extension, which records through the builder's Recorder. */
        template <class F>
        constexpr bool accepts_extension() noexcept
        {
            constexpr bool accepted = std::is_invocable_v<F, Recorder&>;
            static_assert(accepted,
                "extend(extension) takes an extension invoked once as "
                "extension(SushiRuntime::Recorder&), which records its nodes through the "
                "recorder it is handed.");
            return accepted;
        }

        /**
         * @brief Checks the arguments between a launch and its callable.
         * @tparam Captures Every argument after the launch except the last one.
         */
        template <class... Captures>
        constexpr bool accepts_captures() noexcept
        {
            constexpr bool accepted = (is_capture_v<Captures> && ...);
            static_assert(accepted,
                "add(launch, In/Out/InOut..., kernel) takes only In(buffer), Out(buffer) or "
                "InOut(buffer) between the launch and the callable, and the callable comes "
                "last.");
            return accepted;
        }
    } // namespace Detail
} // namespace SushiRuntime
