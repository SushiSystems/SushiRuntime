/**************************************************************************/
/* builder_surface.hpp                                                    */
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
 * @file builder_surface.hpp
 * @brief Declares BuilderSurface, the add() overloads a graph and a region share.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/callable_checks.hpp>
#include <SushiRuntime/api/detail/emit_captured.hpp>
#include <SushiRuntime/api/detail/emit_launch.hpp>
#include <SushiRuntime/api/graph/access.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/api/launch/extent.hpp>
#include <SushiRuntime/api/vocabulary/coupled.hpp>
#include <SushiRuntime/api/vocabulary/dependencies.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/api/vocabulary/placement.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    /**
     * @brief Gives a type that owns a Recorder one fluent verb per kind of node, each
     *        returning it.
     *
     * A verb returns the builder, so a recording the recorder refuses is kept for refusal()
     * and records nothing.
     * @tparam Derived The builder; it has a public recorder() that returns its Recorder.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    template <class Derived>
    class BuilderSurface
    {
        public:
            /**
             * @brief Adds a kernel over a launch, ordered by the handles in Reads and Writes.
             * @tparam Launch An element count, an Extent<N> or an NdRange<N>.
             * @param kernel Callable invoked as kernel(index); a count also takes
             *        kernel(begin, end, sycl::handler&).
             * @param options What the node states beyond its launch and its access.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class Launch, class F,
                      std::enable_if_t<Detail::is_launch_v<Launch>, int> = 0>
            Derived& add(Launch launch, const Reads& reads, const Writes& writes, F&& kernel,
                         NodeOptions options = {})
            {
                if constexpr (Detail::accepts_kernel<Detail::launch_t<Launch>, F>())
                    sink().kernel(Detail::launch_of(launch), Access(reads, writes),
                                  std::forward<F>(kernel), options);
                return self();
            }

            /**
             * @brief Adds a kernel that registers its captures and receives their pointers.
             * @tparam Launch An element count, an Extent<N> or an NdRange<N>.
             * @param rest Further In, Out or InOut captures, then the callable
             *        kernel(index, pointers...), then an optional NodeOptions.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class Launch, class Capture, class... Rest,
                      std::enable_if_t<Detail::is_launch_v<Launch> &&
                                       Detail::is_capture_v<Capture> &&
                                       (sizeof...(Rest) >= 1), int> = 0>
            Derived& add(Launch launch, Capture first, Rest&&... rest)
            {
                auto arguments = std::forward_as_tuple(first, std::forward<Rest>(rest)...);
                constexpr std::size_t captures =
                    sizeof...(Rest) - (Detail::ends_in_node_options_v<Rest...> ? 1 : 0);
                return add_captured(Detail::launch_of(launch), arguments,
                                    std::make_index_sequence<captures>{});
            }

            /**
             * @brief Adds the per-step update rule of a State over its whole field.
             * @param state The state this rule evolves; alive when the graph runs and
             *        free to move after add_rule().
             * @param kernel Callable invoked as kernel(i, const T* current, T* next).
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <typename T, class F>
            Derived& add_rule(State<T>& state, F&& kernel, NodeOptions options = {})
            {
                if constexpr (Detail::accepts_state_rule<std::size_t, T, F>())
                    sink().state_rule(state, std::forward<F>(kernel), options);
                return self();
            }

            /**
             * @brief Adds the per-step update rule of a State over a launch.
             * @tparam Launch An element count, an Extent<N> or an NdRange<N>.
             * @param state Alive when the graph runs; free to move after add_rule().
             * @param kernel Callable invoked as kernel(index, const T* current, T* next).
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class Launch, typename T, class F,
                      std::enable_if_t<Detail::is_launch_v<Launch>, int> = 0>
            Derived& add_rule(Launch launch, State<T>& state, F&& kernel,
                              NodeOptions options = {})
            {
                if constexpr (Detail::accepts_state_rule<Detail::launch_t<Launch>, T, F>())
                    sink().state_rule(state, Detail::launch_of(launch),
                                      std::forward<F>(kernel), options, K_ADD_LAUNCH_STATE);
                return self();
            }

            /**
             * @brief Adds a rule that writes one State from several, over its whole field.
             * @param states Coupled(out, in0, ...); every State is alive when the graph
             *        runs and free to move after add_coupled().
             * @param kernel Callable invoked as kernel(i, current, next, in0, ...).
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <typename Out, typename... Inputs, class F>
            Derived& add_coupled(Coupled<Out, Inputs...> states, F&& kernel,
                                 NodeOptions options = {})
            {
                if constexpr (Detail::accepts_coupled_rule<std::size_t, F, Out, Inputs...>())
                    sink().coupled_rule(states, std::forward<F>(kernel), options,
                                        K_ADD_COUPLED);
                return self();
            }

            /**
             * @brief Adds a rule that writes one State from several, over a launch.
             * @tparam Launch An element count, an Extent<N> or an NdRange<N>.
             * @param states Coupled(out, in0, ...); every State is alive when the graph
             *        runs and free to move after add_coupled().
             * @param kernel Callable invoked as kernel(index, current, next, in0, ...).
             */
            template <class Launch, typename Out, typename... Inputs, class F,
                      std::enable_if_t<Detail::is_launch_v<Launch>, int> = 0>
            Derived& add_coupled(Launch launch, Coupled<Out, Inputs...> states, F&& kernel,
                                 NodeOptions options = {})
            {
                if constexpr (Detail::accepts_coupled_rule<Detail::launch_t<Launch>, F, Out,
                                                           Inputs...>())
                    sink().coupled_rule(Detail::launch_of(launch), states,
                                        std::forward<F>(kernel), options,
                                        K_ADD_LAUNCH_COUPLED);
                return self();
            }

            /**
             * @brief Adds a raw kernel ordered by its declared regions.
             * @param kernel Callable invoked as kernel(sycl::handler&); it submits the whole
             *        command group.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class F>
            Derived& add(const Reads& reads, const Writes& writes, F&& kernel,
                         NodeOptions options = {})
            {
                if constexpr (Detail::accepts_raw_kernel<F>())
                    sink().raw_kernel(Access(reads, writes), std::forward<F>(kernel), options);
                return self();
            }

            /**
             * @brief Adds a late-bound per-element task whose window varies per step;
             *        late binding exists for these two forms only.
             * @param capacity Upper bound the live window must fit.
             * @param kernel Callable invoked as kernel(i) for each live index.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class F>
            Derived& add_dynamic(Dynamic window, std::size_t capacity, const Reads& reads,
                                 const Writes& writes, F&& kernel, NodeOptions options = {})
            {
                if constexpr (Detail::accepts_per_element<F>())
                    sink().dynamic_kernel(std::move(window), capacity, Access(reads, writes),
                                          std::forward<F>(kernel), options);
                return self();
            }

            /**
             * @brief Adds a late-bound per-step update rule for a State; late binding
             *        exists for these two forms only.
             * @param state The state this rule evolves; its size is the capacity.
             * @param kernel Callable invoked as kernel(i, const T* current, T* next).
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <typename T, class F>
            Derived& add_dynamic(Dynamic window, State<T>& state, F&& kernel,
                                 NodeOptions options = {})
            {
                if constexpr (Detail::accepts_state_rule<std::size_t, T, F>())
                    sink().dynamic_kernel(std::move(window), state, std::forward<F>(kernel),
                                          options);
                return self();
            }

            /**
             * @brief Adds a kernel that names no region and runs on device 0.
             * @tparam Launch An element count, an Extent<N> or an NdRange<N>.
             * @param kernel Callable invoked as kernel(index); a count also takes
             *        kernel(begin, end, sycl::handler&).
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class Launch, class F,
                      std::enable_if_t<Detail::is_launch_v<Launch>, int> = 0>
            Derived& add_untracked(Launch launch, F&& kernel, NodeOptions options = {})
            {
                if constexpr (Detail::accepts_kernel<Detail::launch_t<Launch>, F>())
                    sink().untracked_kernel(Detail::launch_of(launch),
                                            std::forward<F>(kernel), On(DeviceIndex{0}),
                                            options, K_ADD_UNTRACKED_LAUNCH);
                return self();
            }

            /**
             * @brief Adds a kernel that names no region and runs on the device stated.
             * @tparam Launch An element count, an Extent<N> or an NdRange<N>.
             * @param placement On(buffer), On(state) or On(DeviceIndex).
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class Launch, class F,
                      std::enable_if_t<Detail::is_launch_v<Launch>, int> = 0>
            Derived& add_untracked(Launch launch, On placement, F&& kernel,
                                   NodeOptions options = {})
            {
                if constexpr (Detail::accepts_kernel<Detail::launch_t<Launch>, F>())
                    sink().untracked_kernel(Detail::launch_of(launch),
                                            std::forward<F>(kernel), placement, options,
                                            K_ADD_UNTRACKED_LAUNCH_ON);
                return self();
            }

            /**
             * @brief Adds a raw kernel that names no region and runs on device 0.
             * @param kernel Callable invoked as kernel(sycl::handler&).
             */
            template <class F>
            Derived& add_untracked(F&& kernel, NodeOptions options = {})
            {
                if constexpr (Detail::accepts_raw_kernel<F>())
                    sink().untracked_raw_kernel(std::forward<F>(kernel), On(DeviceIndex{0}),
                                                options, K_ADD_UNTRACKED_RAW);
                return self();
            }

            /**
             * @brief Adds a raw kernel that names no region and runs on the device stated.
             * @param placement On(buffer), On(state) or On(DeviceIndex).
             * @param kernel Callable invoked as kernel(sycl::handler&).
             */
            template <class F>
            Derived& add_untracked(On placement, F&& kernel, NodeOptions options = {})
            {
                if constexpr (Detail::accepts_raw_kernel<F>())
                    sink().untracked_raw_kernel(std::forward<F>(kernel), placement, options,
                                                K_ADD_UNTRACKED_RAW_ON);
                return self();
            }

            /**
             * @brief Adds a pure-CPU task ordered by the data it touches.
             * @param body Nullary callable run once per step; must not submit SYCL work.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class F>
            Derived& add_host(const Reads& reads, const Writes& writes, F&& body,
                              NodeOptions options = {})
            {
                if constexpr (Detail::accepts_host_task<F>())
                    sink().host(Access(reads, writes), std::forward<F>(body), options);
                return self();
            }

            /**
             * @brief Adds an untracked pure-CPU task that runs every step.
             * @param body Nullary callable run once per step; must not submit SYCL work.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class F>
            Derived& add_host(F&& body, NodeOptions options = {})
            {
                if constexpr (Detail::accepts_host_task<F>())
                    sink().host(Access(), std::forward<F>(body), options);
                return self();
            }

            /**
             * @brief Adds a task that submits its own work, ordered by the data it touches.
             * @param body Callable invoked once per step as
             *        body(sycl::queue&, const std::vector<sycl::event>&); it waits for nothing
             *        and returns the event that ends its work.
             * @param options What the node states beyond its access.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class F>
            Derived& add_task(const Reads& reads, const Writes& writes, F&& body,
                              NodeOptions options = {})
            {
                if constexpr (Detail::accepts_task<F>())
                    sink().task(Access(reads, writes), std::forward<F>(body), options);
                return self();
            }

            /**
             * @brief Runs an extension once; the nodes it records form one rule.
             * @param extension Callable invoked as extension(Recorder&).
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class F>
            Derived& extend(F&& extension)
            {
                if constexpr (Detail::accepts_extension<F>())
                {
                    Recorder& recorder = sink();
                    const Recorder::RuleScope scope = recorder.rule();
                    std::forward<F>(extension)(recorder);
                }
                return self();
            }

            /**
             * @brief Names the rule most recently added; every node of it reports the name.
             * @param label Copied, so a temporary may be passed; an empty label returns the
             *        rule to the default name. Ignored when nothing has been added.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            Derived& name(std::string_view label)
            {
                sink().name_last_rule(label);
                return self();
            }

            /**
             * @brief Names the rule most recently added from a C string.
             * @param label Copied; null counts as an empty label.
             */
            Derived& name(const char* label)
            {
                return name(label != nullptr ? std::string_view(label) : std::string_view());
            }

            /**
             * @brief Returns how many rules have been added.
             * @return One per call of an add verb, and one per extend() call or
             *         Recorder::rule() scope that recorded a node.
             */
            [[nodiscard]] std::size_t size() const noexcept { return view().rule_count(); }

            /** @brief Returns how many nodes the rules record. */
            [[nodiscard]] std::size_t node_count() const noexcept
            {
                return view().node_count();
            }

            /**
             * @brief Returns the first recording this builder refused since clear().
             * @return The error the next run returns, or null when nothing was refused.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] const Error* refusal() const noexcept { return view().refusal(); }

            /**
             * @brief Returns the position of the rule most recently added.
             * @pre size() is not zero.
             */
            [[nodiscard]] std::size_t last_rule() const noexcept { return size() - 1; }

        protected:
            BuilderSurface() = default;
            BuilderSurface(const BuilderSurface&) = default;
            BuilderSurface(BuilderSurface&&) = default;
            BuilderSurface& operator=(const BuilderSurface&) = default;
            BuilderSurface& operator=(BuilderSurface&&) = default;
            ~BuilderSurface() = default;

        private:
            /// @brief Names add_rule(launch, state, kernel) in the messages of its refusals.
            static constexpr const char* K_ADD_LAUNCH_STATE = "add_rule(launch, state, kernel)";
            /// @brief Names add_coupled(Coupled(out, in0, ...), kernel) in its refusals.
            static constexpr const char* K_ADD_COUPLED =
                "add_coupled(Coupled(out, in0, ...), kernel)";
            /// @brief Names add_coupled(launch, Coupled(out, in0, ...), kernel) in its refusals.
            static constexpr const char* K_ADD_LAUNCH_COUPLED =
                "add_coupled(launch, Coupled(out, in0, ...), kernel)";
            /// @brief Names add_untracked(launch, kernel) in the messages of its refusals.
            static constexpr const char* K_ADD_UNTRACKED_LAUNCH =
                "add_untracked(launch, kernel)";
            /// @brief Names add_untracked(launch, On(...), kernel) in its refusals.
            static constexpr const char* K_ADD_UNTRACKED_LAUNCH_ON =
                "add_untracked(launch, On(...), kernel)";
            /// @brief Names add_untracked(kernel) in the messages of its refusals.
            static constexpr const char* K_ADD_UNTRACKED_RAW = "add_untracked(kernel)";
            /// @brief Names add_untracked(On(...), kernel) in the messages of its refusals.
            static constexpr const char* K_ADD_UNTRACKED_RAW_ON =
                "add_untracked(On(...), kernel)";

            /** @brief Returns the builder this surface belongs to. */
            Derived& self() noexcept { return static_cast<Derived&>(*this); }

            /** @brief Returns the recorder every overload records onto. */
            Recorder& sink() { return self().recorder(); }

            /** @brief Returns the recorder the queries read, without recording on it. */
            const Recorder& view() const noexcept
            {
                return static_cast<const Derived&>(*this).recorder();
            }

            /**
             * @brief Records the capture overload once the callable is split from the
             *        captures.
             * @tparam Arguments A std::tuple of references: sizeof...(Is) captures, then the
             *         callable, then an optional NodeOptions.
             */
            template <class Launch, class Arguments, std::size_t... Is>
            Derived& add_captured(Launch launch, Arguments& arguments,
                                  std::index_sequence<Is...>)
            {
                using F = std::tuple_element_t<sizeof...(Is), Arguments>;
                if constexpr (Detail::accepts_captures<
                                  std::tuple_element_t<Is, Arguments>...>())
                    if constexpr (Detail::accepts_captured_kernel<
                                      Launch, F, decltype(std::get<Is>(arguments).ptr)...>())
                        sink().captured_kernel(
                            launch, Detail::captured_access(std::get<Is>(arguments)...),
                            std::make_tuple(std::get<Is>(arguments).ptr...),
                            std::forward<F>(std::get<sizeof...(Is)>(arguments)),
                            options_after<sizeof...(Is)>(arguments));
                return self();
            }

            /**
             * @brief Returns the NodeOptions that follows the callable, or a default one.
             * @tparam Callable Position of the callable in Arguments.
             */
            template <std::size_t Callable, class Arguments>
            static NodeOptions options_after(Arguments& arguments)
            {
                if constexpr (std::tuple_size_v<Arguments> > Callable + 1)
                    return std::get<Callable + 1>(arguments);
                else
                    return NodeOptions();
            }

    };
} // namespace SushiRuntime
