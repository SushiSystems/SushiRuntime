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
#include <initializer_list>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/callable_traits.hpp>
#include <SushiRuntime/api/graph/access.hpp>
#include <SushiRuntime/api/graph/backing_graph.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/api/launch/extent.hpp>
#include <SushiRuntime/api/launch/nd_range.hpp>
#include <SushiRuntime/api/vocabulary/captures.hpp>
#include <SushiRuntime/api/vocabulary/dependencies.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/task/task_types.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Gives a type that owns a Recorder the fluent add() overloads, each returning it.
         * @tparam Derived The builder; it has a public recorder() that returns its Recorder.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        template <class Derived>
        class BuilderSurface
        {
            public:
                /**
                 * @brief Adds a per-element task that records no regions and is ordered against
                 *        nothing.
                 * @param fn Callable invoked as fn(i) for each index i.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Derived& add_untracked(std::size_t n, F&& fn)
                {
                    sink().untracked_kernel(n, std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a range task that records no regions; fn receives bounds and handler.
                 * @param n Upper bound; fn is called as fn(0, n, handler).
                 */
                template <class F, std::enable_if_t<is_range_kernel_v<F>, int> = 0>
                Derived& add_untracked(std::size_t n, F&& fn)
                {
                    sink().untracked_kernel(n, std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a raw kernel that records no regions and drives the handler itself.
                 * @param fn Callable invoked as fn(sycl::handler&).
                 */
                template <class F, std::enable_if_t<is_raw_kernel_v<F>, int> = 0>
                Derived& add_untracked(F&& fn)
                {
                    sink().untracked_raw_kernel(std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds an emitter that records its own nodes on the backing graph.
                 * @param emit Callable invoked as emit(BackingGraph&) at every build of the plan.
                 * @pre emit only adds nodes: it does not compile, drain or keep the graph.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_graph_emitter_v<F>, int> = 0>
                Derived& add_emitter(F&& emit)
                {
                    sink().emitter(std::forward<F>(emit));
                    return self();
                }

                /**
                 * @brief Adds a multidimensional task over an Extent that records no regions.
                 * @tparam N Iteration rank (1, 2, or 3).
                 * @param fn Callable invoked as fn(sycl::id<N>) per work-item.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <int N, class F,
                          std::enable_if_t<std::is_invocable_v<F, sycl::id<N>>, int> = 0>
                Derived& add_untracked(Extent<N> e, F&& fn)
                {
                    sink().untracked_kernel(e, std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a tiled task over an NdRange that records no regions.
                 * @tparam N Iteration rank (1, 2, or 3).
                 * @param r Global and local ranges; global must tile by local.
                 * @param fn Callable invoked as fn(sycl::nd_item<N>) per work-item.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <int N, class F,
                          std::enable_if_t<std::is_invocable_v<F, sycl::nd_item<N>>, int> = 0>
                Derived& add_untracked(NdRange<N> r, F&& fn)
                {
                    sink().untracked_kernel(r, std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a 1-D per-element task ordered by the handles in Reads and Writes.
                 * @param n Iteration range; fn is called for each i in [0, n).
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Derived& add(const Reads& reads, const Writes& writes,
                           std::size_t n, F&& fn)
                {
                    sink().kernel(n, Access(reads, writes), std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds an N-D per-element task over an Extent, ordered by Reads and Writes.
                 * @tparam N Iteration rank (1, 2, or 3).
                 * @param fn Callable invoked as fn(sycl::id<N>) per work-item.
                 */
                template <int N, class F,
                          std::enable_if_t<std::is_invocable_v<F, sycl::id<N>>, int> = 0>
                Derived& add(const Reads& reads, const Writes& writes,
                           Extent<N> e, F&& fn)
                {
                    sink().kernel(e, Access(reads, writes), std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a tiled NdRange task ordered by the regions it declares.
                 * @tparam N Iteration rank (1, 2, or 3).
                 * @param r Global and local ranges; global must tile by local.
                 * @param fn Callable invoked as fn(sycl::nd_item<N>) per work-item.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <int N, class F,
                          std::enable_if_t<std::is_invocable_v<F, sycl::nd_item<N>>, int> = 0>
                Derived& add(const Reads& reads, const Writes& writes,
                           NdRange<N> r, F&& fn)
                {
                    sink().kernel(r, Access(reads, writes), std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a range task ordered by its declared regions; fn drives the handler
                 *        over [0, n).
                 * @param fn Callable invoked as fn(begin, end, sycl::handler&).
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_range_kernel_v<F>, int> = 0>
                Derived& add(const Reads& reads, const Writes& writes,
                           std::size_t n, F&& fn)
                {
                    sink().kernel(n, Access(reads, writes), std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a raw kernel ordered by its declared regions; fn owns the whole
                 *        command group.
                 * @param fn Callable invoked as fn(sycl::handler&).
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_raw_kernel_v<F>, int> = 0>
                Derived& add(const Reads& reads, const Writes& writes, F&& fn)
                {
                    sink().raw_kernel(Access(reads, writes), std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds an N-D per-element task that registers its captures and passes their
                 *        pointers.
                 * @tparam N Iteration rank (1, 2, or 3).
                 * @param args Remaining captured arguments, with the callable last.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <int N, class Cap0, class... Args,
                          std::enable_if_t<detail::is_capture_v<Cap0> &&
                                           (sizeof...(Args) >= 1), int> = 0>
                Derived& add(Extent<N> e, Cap0 c0, Args&&... args)
                {
                    // The last element of Args is the callable; the others are captures.
                    auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
                    constexpr std::size_t M = sizeof...(Args);
                    return add_captured(e, c0, tup, std::make_index_sequence<M - 1>{},
                                        std::get<M - 1>(tup));
                }

                /**
                 * @brief Adds the per-step update rule of a State: reads current, writes next.
                 * @param s The state this rule evolves; must outlive the graph.
                 * @param fn Callable invoked as fn(i, const T* cur, T* next) for each i in [0,
                 *        s.size()).
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <typename T, class F,
                          std::enable_if_t<std::is_invocable_v<F, std::size_t,
                                                               const T*, T*>, int> = 0>
                Derived& add(State<T>& s, F&& fn)
                {
                    sink().state_rule(s, std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a multidimensional per-step update rule for a State over an Extent.
                 * @param s The state this rule evolves; must outlive the graph.
                 * @param fn Callable invoked as fn(sycl::id<N>, const T* cur, T* next).
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <typename T, int N, class F,
                          std::enable_if_t<std::is_invocable_v<F,
                              sycl::id<N>, const T*, T*>, int> = 0>
                Derived& add(State<T>& s, Extent<N> e, F&& fn)
                {
                    sink().state_rule(s, e, std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a coupled update rule that writes out's next field from several
                 *        current fields.
                 * @param out The field this rule writes; must outlive the graph.
                 * @param in0 First read field; must outlive the graph.
                 * @param rest Further read fields, then the callable fn(i, cur, next, a, ...) last.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <typename T, typename A, class... Rest,
                          std::enable_if_t<(sizeof...(Rest) >= 1), int> = 0>
                Derived& add(State<T>& out, State<A>& in0, Rest&&... rest)
                {
                    // The last element of Rest is the callable; the others are read States.
                    auto tup = std::forward_as_tuple(std::forward<Rest>(rest)...);
                    constexpr std::size_t M = sizeof...(Rest);
                    return add_coupled(out, in0, tup, std::make_index_sequence<M - 1>{},
                                       std::get<M - 1>(tup));
                }

                /**
                 * @brief Adds a per-element task ordered by the raw base pointers it reads and
                 *        writes.
                 * @param n Iteration range; fn is called for each i in [0, n).
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Derived& add(const std::vector<void*>& reads,
                           const std::vector<void*>& writes,
                           std::size_t n, F&& fn)
                {
                    // A bare base pointer names the whole-buffer region.
                    using Regions = std::vector<Core::ResourceRegion>;
                    sink().kernel(n, Access(Regions(reads.begin(), reads.end()),
                                            Regions(writes.begin(), writes.end())),
                                  std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a late-bound per-element task whose size and enablement vary per
                 *        step.
                 * @param capacity Pre-sized upper bound; the live size must not exceed it.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Derived& add(Dynamic dyn, const Reads& reads, const Writes& writes,
                           std::size_t capacity, F&& fn)
                {
                    sink().kernel(std::move(dyn), capacity, Access(reads, writes),
                                  std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a late-bound per-element task that names its data by raw base
                 *        pointer.
                 * @param capacity Pre-sized upper bound; the live size must not exceed it.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Derived& add(Dynamic dyn, const std::vector<void*>& reads,
                           const std::vector<void*>& writes, std::size_t capacity, F&& fn)
                {
                    using Regions = std::vector<Core::ResourceRegion>;
                    sink().kernel(std::move(dyn), capacity,
                                  Access(Regions(reads.begin(), reads.end()),
                                         Regions(writes.begin(), writes.end())),
                                  std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a late-bound per-step update rule for a State.
                 * @param s The state this rule evolves; must outlive the graph.
                 * @param fn Callable invoked as fn(i, const T* cur, T* next).
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <typename T, class F,
                          std::enable_if_t<std::is_invocable_v<F, std::size_t,
                                                               const T*, T*>, int> = 0>
                Derived& add(Dynamic dyn, State<T>& s, F&& fn)
                {
                    sink().state_rule(std::move(dyn), s, std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds a pure-CPU task ordered by the data it touches.
                 * @param fn Nullary callable run once; must not submit SYCL work.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<std::is_invocable_v<F>, int> = 0>
                Derived& add_host(const Reads& reads, const Writes& writes, F&& fn)
                {
                    sink().host(Access(reads, writes), std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Adds an untracked pure-CPU task that runs every step.
                 * @param fn Nullary callable run once per step; must not submit SYCL work.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<std::is_invocable_v<F>, int> = 0>
                Derived& add_host(F&& fn)
                {
                    sink().host(Access(), std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Names the task most recently added to this graph.
                 * @param name Persistent label, stored not copied; ignored when no task has been
                 *        added.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                Derived& name_last(const char* name) noexcept
                {
                    sink().name_last_node(name);
                    return self();
                }

                /**
                 * @brief Adds an offloadable task: a named remote kernel with a local fallback.
                 * @param op OpID of the kernel, e.g. "vec.add"_op.
                 * @param local Local fallback invoked as local(i) for i in [0, n).
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Derived& add(::SushiRuntime::Graph::OpID op, std::size_t n,
                           const std::vector<void*>& reads,
                           const std::vector<void*>& writes,
                           F&& local,
                           std::initializer_list<std::uint64_t> params = {})
                {
                    using Regions = std::vector<Core::ResourceRegion>;
                    sink().offload(op, n,
                                   Access(Regions(reads.begin(), reads.end()),
                                          Regions(writes.begin(), writes.end())),
                                   std::forward<F>(local), std::vector<std::uint64_t>(params));
                    return self();
                }

                /**
                 * @brief Adds an offloadable task that names its data with Reads and Writes.
                 * @param local Local fallback invoked as local(i) for i in [0, n).
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Derived& add(::SushiRuntime::Graph::OpID op, std::size_t n,
                           const Reads& reads, const Writes& writes,
                           F&& local,
                           std::initializer_list<std::uint64_t> params = {})
                {
                    sink().offload(op, n, Access(reads, writes), std::forward<F>(local),
                                   std::vector<std::uint64_t>(params));
                    return self();
                }

                /**
                 * @brief Adds an offloadable per-step update rule for a State.
                 * @pre s was registered with State::distributed().
                 * @param s The state this rule evolves; must outlive the graph.
                 * @param local Local fallback invoked as local(i, const T* cur, T* next).
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <typename T, class F,
                          std::enable_if_t<std::is_invocable_v<F, std::size_t,
                                                               const T*, T*>, int> = 0>
                Derived& add(::SushiRuntime::Graph::OpID op, State<T>& s, F&& local,
                           std::initializer_list<std::uint64_t> params = {})
                {
                    sink().offload_rule(op, s, std::forward<F>(local),
                                        std::vector<std::uint64_t>(params));
                    return self();
                }

            protected:
                BuilderSurface() = default;
                BuilderSurface(const BuilderSurface&) = default;
                BuilderSurface(BuilderSurface&&) = default;
                BuilderSurface& operator=(const BuilderSurface&) = default;
                BuilderSurface& operator=(BuilderSurface&&) = default;
                ~BuilderSurface() = default;

            private:
                /** @brief Returns the builder this surface belongs to. */
                Derived& self() noexcept { return static_cast<Derived&>(*this); }

                /** @brief Returns the recorder every overload records onto. */
                Recorder& sink() noexcept { return self().recorder(); }

                /**
                 * @brief Appends a capture's region to what it reads, what it writes, or both, and
                 *        watches its allocation.
                 */
                template <class Cap>
                static void collect(Access& access, const Cap& c)
                {
                    if (Cap::reads)
                        access.reads.push_back(c.key);
                    if (Cap::writes)
                        access.writes.push_back(c.key);
                    if (!c.life.expired())
                        access.lives.push_back(c.life);
                }

                /**
                 * @brief Records the capture overload once the callable is split from the
                 *        captures.
                 */
                template <int N, class Cap0, class Tuple, std::size_t... Is, class F>
                Derived& add_captured(Extent<N> e, Cap0 c0, Tuple& tup,
                                      std::index_sequence<Is...>, F&& fn)
                {
                    Access access;
                    collect(access, c0);
                    (collect(access, std::get<Is>(tup)), ...);
                    sink().captured_kernel(e, std::move(access),
                                           std::make_tuple(c0.ptr, std::get<Is>(tup).ptr...),
                                           std::forward<F>(fn));
                    return self();
                }

                /**
                 * @brief Records the coupled-State overload once the callable is split from the
                 *        read States.
                 */
                template <typename T, typename A, class Tuple, std::size_t... Is, class F>
                Derived& add_coupled(State<T>& out, State<A>& in0, Tuple& tup,
                                     std::index_sequence<Is...>, F&& fn)
                {
                    sink().coupled_rule(out, std::forward<F>(fn), in0, std::get<Is>(tup)...);
                    return self();
                }
        };

    } // namespace API
} // namespace SushiRuntime
