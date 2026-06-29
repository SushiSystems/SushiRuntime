/**************************************************************************/
/* graph.hpp                                                              */
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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/launch/extent.hpp>
#include <SushiRuntime/api/launch/nd_range.hpp>
#include <SushiRuntime/api/detail/run_report_builder.hpp>
#include <SushiRuntime/api/detail/callable_traits.hpp>
#include <SushiRuntime/api/detail/step_machinery.hpp>
#include <SushiRuntime/api/vocabulary/dependencies.hpp>
#include <SushiRuntime/api/vocabulary/captures.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/resource_liveness.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/graph/task_types.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    namespace API
    {
        // The lower-level graph builder this layer wraps. Spelled out fully to
        // avoid clashing with the API::Graph class name below.
        using BackingGraph = ::SushiRuntime::Graph::TaskGraph;

        // The dynamic (region-based) graph and its mutation unit. Forward-declared
        // so the static Graph can befriend them: a Region reuses Graph add()
        // surface by deriving from it, and the DynamicGraph composer reads a
        // recorded specs and per-step updaters to stitch one plan.
        class Region;
        class DynamicGraph;

        /**
         * @brief A chainable graph of tasks built fluently, then run.
         *
         * Every add() overload appends one task and returns *this so calls chain.
         * The concept-constrained overloads give a readable compiler error when a
         * callable has the wrong signature — something a macro cannot do.
         * Obtain one from Runtime::graph(); it is moveable but not copyable.
         */
        class Graph
        {
            public:
                Graph(Graph&&) = default;
                Graph& operator=(Graph&&) = default;
                Graph(const Graph&) = delete;
                Graph& operator=(const Graph&) = delete;

                /**
                 * @brief Adds a per-element task run as a parallel_for over [0, n).
                 * @param n  Number of elements (the iteration range).
                 * @param fn Callable invoked as fn(i) for each index i.
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Graph& add(std::size_t n, F&& fn)
                {
                    specs_.push_back([fn = std::forward<F>(fn), n](BackingGraph& bg)
                    {
                        bg.add_node([fn, n](sycl::handler& h)
                        {
                            h.parallel_for(sycl::range<1>(n),
                                           [fn](sycl::id<1> idx) { fn(idx[0]); });
                        });
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a range task; fn receives the bounds and the handler.
                 * @param n  Upper bound; fn is called as fn(0, n, handler).
                 * @param fn Callable invoked as fn(begin, end, sycl::handler&).
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_range_kernel_v<F>, int> = 0>
                Graph& add(std::size_t n, F&& fn)
                {
                    specs_.push_back([fn = std::forward<F>(fn), n](BackingGraph& bg)
                    {
                        bg.add_node([fn, n](sycl::handler& h)
                        {
                            fn(std::size_t{0}, n, h);
                        });
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a raw kernel that drives the sycl::handler itself.
                 * @param fn Callable invoked as fn(sycl::handler&).
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_raw_kernel_v<F>, int> = 0>
                Graph& add(F&& fn)
                {
                    specs_.push_back([fn = std::forward<F>(fn)](BackingGraph& bg)
                    {
                        bg.add_node(fn);
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a multidimensional per-element task over an Extent.
                 *
                 * The 2-D/3-D counterpart of add(std::size_t, fn): the runtime
                 * submits a parallel_for over the Extent's box and invokes @p fn with
                 * the global sycl::id of each work-item. When the Extent carries an
                 * origin offset the iteration covers only that sub-box, so a kernel
                 * can skip a halo or touch just a ghost layer. Plain 1-D counts keep
                 * using the std::size_t overloads; pass a 1-D Extent only when you
                 * want an origin offset.
                 *
                 * @tparam N  Iteration rank (1, 2, or 3).
                 * @param e   The iteration box (extent and origin offset).
                 * @param fn  Callable invoked as fn(sycl::id<N>) per work-item.
                 * @return *this, for chaining.
                 */
                template <int N, class F,
                          std::enable_if_t<std::is_invocable_v<F, sycl::id<N>>, int> = 0>
                Graph& add(Extent<N> e, F&& fn)
                {
                    specs_.push_back([fn = std::forward<F>(fn), e](BackingGraph& bg)
                    {
                        bg.add_node([fn, e](sycl::handler& h)
                        {
                            const sycl::id<N> off = e.offset;
                            h.parallel_for(e.extent,
                                           [fn, off](sycl::id<N> idx) { fn(idx + off); });
                        });
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a tiled task over an NdRange (work-group execution).
                 *
                 * Submits a parallel_for over the NdRange and hands @p fn a
                 * sycl::nd_item<N>, exposing the work-group model — barriers, local
                 * (shared) memory, sub-groups — that a flat parallel_for cannot. Use
                 * it for blocked algorithms (tiled GEMM, shared-memory stencils)
                 * where neighbouring work-items must cooperate.
                 *
                 * @tparam N  Iteration rank (1, 2, or 3).
                 * @param r   Global and local ranges; global must tile by local.
                 * @param fn  Callable invoked as fn(sycl::nd_item<N>) per work-item.
                 * @return *this, for chaining.
                 */
                template <int N, class F,
                          std::enable_if_t<std::is_invocable_v<F, sycl::nd_item<N>>, int> = 0>
                Graph& add(NdRange<N> r, F&& fn)
                {
                    specs_.push_back([fn = std::forward<F>(fn), r](BackingGraph& bg)
                    {
                        bg.add_node([fn, r](sycl::handler& h)
                        {
                            h.parallel_for(r.to_nd_range(),
                                           [fn](sycl::nd_item<N> it) { fn(it); });
                        });
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a 1-D per-element task with handle-based read/write
                 *        tracking.
                 *
                 * The ergonomic form of the explicit-tracking overload: pass the
                 * Buffers/States the task reads and writes as Reads(...)/Writes(...)
                 * instead of building a std::vector<void*> by hand. The dependency
                 * tracker still orders the task by the keys those wrappers gathered.
                 *
                 * @param reads  Handles this task reads (their read keys).
                 * @param writes Handles this task writes (their write keys).
                 * @param n      Iteration range; fn is called for each i in [0, n).
                 * @param fn     Callable invoked as fn(i) for each index i.
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Graph& add(const Reads& reads, const Writes& writes,
                           std::size_t n, F&& fn)
                {
                    observe(reads);
                    observe(writes);
                    return emit_per_element_tracked(reads.keys, writes.keys,
                                                    n, std::forward<F>(fn));
                }

                /**
                 * @brief Adds an N-D per-element task with handle-based tracking.
                 *
                 * As the 1-D Reads/Writes overload, over an Extent box so the same
                 * ergonomic dependency wrappers serve multidimensional kernels.
                 *
                 * @tparam N  Iteration rank (1, 2, or 3).
                 * @param reads  Handles this task reads.
                 * @param writes Handles this task writes.
                 * @param e   The iteration box (extent and origin offset).
                 * @param fn  Callable invoked as fn(sycl::id<N>) per work-item.
                 * @return *this, for chaining.
                 */
                template <int N, class F,
                          std::enable_if_t<std::is_invocable_v<F, sycl::id<N>>, int> = 0>
                Graph& add(const Reads& reads, const Writes& writes,
                           Extent<N> e, F&& fn)
                {
                    observe(reads);
                    observe(writes);
                    const std::vector<Core::ResourceRegion> r = reads.keys;
                    const std::vector<Core::ResourceRegion> w = writes.keys;
                    specs_.push_back([fn = std::forward<F>(fn), e, r, w](BackingGraph& bg)
                    {
                        bg.add_node([fn, e](sycl::handler& h)
                        {
                            const sycl::id<N> off = e.offset;
                            h.parallel_for(e.extent,
                                           [fn, off](sycl::id<N> idx) { fn(idx + off); });
                        }, r, w);
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds an N-D per-element task capturing its buffer arguments.
                 *
                 * The most direct form: pass the buffers a kernel touches as
                 * In(buf)/Out(buf)/InOut(buf) and the runtime both registers the
                 * reads/writes and hands @p fn the raw element pointers, in argument
                 * order, after the index. The caller writes the data flow once and
                 * gets pointer capture and dependency ordering together.
                 *
                 * @tparam N    Iteration rank (1, 2, or 3).
                 * @tparam Cap0 First capture wrapper (guarantees at least one).
                 * @tparam Args Remaining capture wrappers, then the trailing callable.
                 * @param e     The iteration box (extent and origin offset).
                 * @param c0    First captured argument.
                 * @param args  Remaining captured arguments and the callable last.
                 * @return *this, for chaining.
                 */
                template <int N, class Cap0, class... Args,
                          std::enable_if_t<detail::is_capture_v<Cap0> &&
                                           (sizeof...(Args) >= 1), int> = 0>
                Graph& add(Extent<N> e, Cap0 c0, Args&&... args)
                {
                    // Args is one trailing pack so it deduces; the last element is the
                    // callable and the rest are further captures. Splitting here keeps
                    // the call site reading In(a), Out(c), fn with fn last.
                    auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
                    constexpr std::size_t M = sizeof...(Args);
                    return emit_captured(e, c0, tup, std::make_index_sequence<M - 1>{},
                                         std::get<M - 1>(tup));
                }

                /**
                 * @brief Adds the per-step update rule for a time-evolving State.
                 *
                 * This is the canonical stepped task: it reads the state's current
                 * field and writes its next field, with the dependency tracker
                 * keying on those two buffers automatically. The runtime supplies
                 * the current/next pointers to @p fn at execution time, so under
                 * run(n) the same compiled node follows the double-buffer swap with
                 * no recompilation. Provide exactly one update rule per State per
                 * step.
                 *
                 * @tparam T  Element type of the State.
                 * @param s   The state this rule evolves; must outlive the graph.
                 * @param fn  Callable invoked as fn(i, const T* cur, T* next) for
                 *            each index i in [0, s.size()).
                 * @return *this, for chaining.
                 */
                template <typename T, class F,
                          std::enable_if_t<std::is_invocable_v<F, std::size_t,
                                                               const T*, T*>, int> = 0>
                Graph& add(State<T>& s, F&& fn)
                {
                    observe_state(s);
                    auto view = steps_.view_for(s);
                    State<T>* sp = &s;

                    specs_.push_back([fn = std::forward<F>(fn), view, sp](BackingGraph& bg)
                    {
                        const std::size_t n = sp->size();
                        bg.add_node([fn, view, n](sycl::handler& h)
                        {
                            // Read fresh each submit (each step) to follow the swap.
                            const T* cur = view->cur;
                            T* next = view->next;
                            h.parallel_for(sycl::range<1>(n),
                                           [=](sycl::id<1> i) { fn(i[0], cur, next); });
                        }, { sp->read_key() }, { sp->write_key() });
                    });

                    steps_.register_advance(s, view);
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a multidimensional per-step update rule for a State.
                 *
                 * The 2-D/3-D form of add(State, fn): the runtime iterates the Extent
                 * box (whole field, or a sub-box for boundary/halo handling) and
                 * hands @p fn the global index alongside the current/next pointers.
                 * The double-buffer swap is followed exactly as in the 1-D form.
                 *
                 * @tparam T  Element type of the State.
                 * @tparam N  Iteration rank (1, 2, or 3).
                 * @param s   The state this rule evolves; must outlive the graph.
                 * @param e   The iteration box (extent and origin offset).
                 * @param fn  Callable invoked as fn(sycl::id<N>, const T* cur, T* next).
                 * @return *this, for chaining.
                 */
                template <typename T, int N, class F,
                          std::enable_if_t<std::is_invocable_v<F,
                              sycl::id<N>, const T*, T*>, int> = 0>
                Graph& add(State<T>& s, Extent<N> e, F&& fn)
                {
                    observe_state(s);
                    auto view = steps_.view_for(s);
                    State<T>* sp = &s;

                    specs_.push_back([fn = std::forward<F>(fn), view, sp, e](BackingGraph& bg)
                    {
                        bg.add_node([fn, view, e](sycl::handler& h)
                        {
                            const T* cur = view->cur;
                            T* next = view->next;
                            const sycl::id<N> off = e.offset;
                            h.parallel_for(e.extent, [=](sycl::id<N> idx)
                            {
                                fn(idx + off, cur, next);
                            });
                        }, { sp->read_key() }, { sp->write_key() });
                    });

                    steps_.register_advance(s, view);
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a coupled update rule reading any number of fields.
                 *
                 * Writes @p out's next field from @p out's current field and the
                 * current field of one or more other States — the building block for
                 * coupled systems (e.g. reaction-diffusion with several species). All
                 * reads see the previous step's data (a Jacobi update): every State
                 * advances only after all of a step's kernels have run, so writing one
                 * field never changes what another reads in the same step. Each field
                 * must have exactly one update rule per step.
                 *
                 * @tparam T    Element type of the written field.
                 * @tparam A    Element type of the first read field.
                 * @tparam Rest Further read States, then the update callable last.
                 * @param out  The field this rule writes; must outlive the graph.
                 * @param in0  First read field; must outlive the graph.
                 * @param rest Further read fields and the callable last. The callable
                 *             is invoked as fn(i, const T* cur, T* next, const A* a,
                 *             ...); one read pointer per input, in order.
                 * @return *this, for chaining.
                 */
                template <typename T, typename A, class... Rest,
                          std::enable_if_t<(sizeof...(Rest) >= 1), int> = 0>
                Graph& add(State<T>& out, State<A>& in0, Rest&&... rest)
                {
                    // Rest is one trailing pack (so it deduces); its last element is
                    // the callable and the rest are further read States. Splitting
                    // here keeps the call site reading add(out, a, b, c, fn).
                    auto tup = std::forward_as_tuple(std::forward<Rest>(rest)...);
                    constexpr std::size_t M = sizeof...(Rest);
                    return emit_coupled(out, in0, tup,
                                        std::make_index_sequence<M - 1>{},
                                        std::get<M - 1>(tup));
                }

                /**
                 * @brief Adds a per-element task with explicit read/write tracking.
                 *
                 * The advanced escape hatch for callers who manage their own data
                 * and want the dependency tracker to order this task against others
                 * by the pointers it touches. Prefer the State/Buffer overloads;
                 * reach for this only when handle-based tracking does not fit.
                 *
                 * @param reads  Base pointers this task reads (e.g. buffer keys).
                 * @param writes Base pointers this task writes.
                 * @param n      Iteration range; fn is called for each i in [0, n).
                 * @param fn     Callable invoked as fn(i) for each index i.
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Graph& add(const std::vector<void*>& reads,
                           const std::vector<void*>& writes,
                           std::size_t n, F&& fn)
                {
                    // A bare base pointer is the whole-buffer region; convert and
                    // funnel through the one tracked-task emitter.
                    return emit_per_element_tracked(
                        std::vector<Core::ResourceRegion>(reads.begin(), reads.end()),
                        std::vector<Core::ResourceRegion>(writes.begin(), writes.end()),
                        n, std::forward<F>(fn));
                }

                /**
                 * @brief Adds a late-bound per-element task with explicit tracking.
                 *
                 * The dynamic counterpart of the Reads/Writes per-element add: the
                 * task is compiled once, but @p dyn lets its iteration size and
                 * whether it runs vary every step with no recompile. Allocate the
                 * touched buffers for the maximum (@p capacity) elements; sized()
                 * then iterates only the live count each step, and when() can skip
                 * the task entirely.
                 *
                 * @param dyn      Late-binding controls from when()/sized().
                 * @param reads    Handles this task reads (their read keys).
                 * @param writes   Handles this task writes (their write keys).
                 * @param capacity Pre-sized upper bound; the live size must not exceed it.
                 * @param fn       Callable invoked as fn(i) for each live index i.
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Graph& add(Dynamic dyn, const Reads& reads, const Writes& writes,
                           std::size_t capacity, F&& fn)
                {
                    observe(reads);
                    observe(writes);
                    return emit_dynamic_per_element(std::move(dyn), reads.keys, writes.keys,
                                                    capacity, std::forward<F>(fn));
                }

                /**
                 * @brief Adds a late-bound per-element task naming data by raw pointer.
                 *
                 * The raw-pointer form of the dynamic per-element add; see the
                 * Reads/Writes overload for the late-binding semantics.
                 *
                 * @param dyn      Late-binding controls from when()/sized().
                 * @param reads    Base pointers this task reads.
                 * @param writes   Base pointers this task writes.
                 * @param capacity Pre-sized upper bound; the live size must not exceed it.
                 * @param fn       Callable invoked as fn(i) for each live index i.
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Graph& add(Dynamic dyn, const std::vector<void*>& reads,
                           const std::vector<void*>& writes, std::size_t capacity, F&& fn)
                {
                    return emit_dynamic_per_element(std::move(dyn),
                        std::vector<Core::ResourceRegion>(reads.begin(), reads.end()),
                        std::vector<Core::ResourceRegion>(writes.begin(), writes.end()),
                        capacity, std::forward<F>(fn));
                }

                /**
                 * @brief Adds a late-bound per-step update rule for a State.
                 *
                 * The dynamic counterpart of add(State, fn): the same read-current /
                 * write-next stepped task, but @p dyn lets its active-element count
                 * and whether it runs vary every step with no recompile. The State's
                 * full allocation is the capacity; sized() iterates a prefix of it
                 * (e.g. a changing number of active cells), and when() can skip the
                 * step's kernel. The double-buffer swap is followed as in the static
                 * form. Provide exactly one update rule per State per step.
                 *
                 * Note: the State still advances (swaps current/next) on a step its
                 * when() disabled, so a skipped step leaves the un-written next field
                 * current. This overload suits size variation; truly sleeping a field
                 * across steps belongs to the streaming layer, not when().
                 *
                 * @tparam T  Element type of the State.
                 * @param dyn Late-binding controls from when()/sized().
                 * @param s   The state this rule evolves; must outlive the graph.
                 * @param fn  Callable invoked as fn(i, const T* cur, T* next).
                 * @return *this, for chaining.
                 */
                template <typename T, class F,
                          std::enable_if_t<std::is_invocable_v<F, std::size_t,
                                                               const T*, T*>, int> = 0>
                Graph& add(Dynamic dyn, State<T>& s, F&& fn)
                {
                    observe_state(s);
                    return emit_dynamic_state(std::move(dyn), s, std::forward<F>(fn));
                }

                /**
                 * @brief Adds a pure-CPU task ordered by the data it touches.
                 *
                 * For arbitrary host C++ that submits no device work — gameplay
                 * logic, AI, navigation, the CPU half of a broadphase. Name the data
                 * it touches with Reads(...)/Writes(...) so the tracker orders it
                 * against the rest of the graph; the runtime then runs @p fn natively
                 * on a worker thread, with no SYCL queue submission, and releases its
                 * successors through the same path as any kernel task. The shared USM
                 * it reads is visible because the task runs only after its
                 * predecessors complete.
                 *
                 * @param reads  Handles this task reads (their read keys).
                 * @param writes Handles this task writes (their write keys).
                 * @param fn     Nullary callable run once; must not submit SYCL work.
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<std::is_invocable_v<F>, int> = 0>
                Graph& add_host(const Reads& reads, const Writes& writes, F&& fn)
                {
                    observe(reads);
                    observe(writes);
                    std::vector<Core::ResourceRegion> r = reads.keys;
                    std::vector<Core::ResourceRegion> w = writes.keys;
                    specs_.push_back([fn = std::forward<F>(fn), r, w](BackingGraph& bg)
                    {
                        bg.add_native(r, w, fn);
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds an untracked pure-CPU task that runs every step.
                 *
                 * The dependency-free form of add_host: with no Reads/Writes the task
                 * is both a root and a leaf, so use it for self-contained host work
                 * (telemetry, a per-step callback) that needs no ordering against
                 * other tasks. Runs natively, with no SYCL submission.
                 *
                 * @param fn Nullary callable run once per step; must not submit SYCL work.
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<std::is_invocable_v<F>, int> = 0>
                Graph& add_host(F&& fn)
                {
                    specs_.push_back([fn = std::forward<F>(fn)](BackingGraph& bg)
                    {
                        bg.add_native({}, {}, fn);
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds an offloadable task: a named remote kernel with a
                 *        local fallback.
                 *
                 * This is the fluent counterpart to a distributed dispatch. @p op
                 * names a kernel registered (by the same OpID) in this binary's
                 * KernelRegistry on every node; when the runtime is a Master with a
                 * live worker that supports @p op, the scheduler may ship this task
                 * over the wire. Otherwise — standalone build, no worker, or the
                 * policy declining — @p local runs it here, so the same graph is
                 * correct with or without a cluster (transparent fallback).
                 *
                 * @p reads / @p writes are the buffers the task touches, given as
                 * dependency keys (Buffer/State::dependency_key() or read_/write_key()).
                 * The dependency tracker orders the task by them, and the distributed
                 * layer resolves them to network buffer handles. @p params are scalar
                 * arguments carried in TaskMetadata (e.g. element counts).
                 *
                 * @param op     OpID of the kernel, e.g. "vec.add"_op.
                 * @param n      Iteration range for the local fallback parallel_for.
                 * @param reads  Dependency keys this task reads.
                 * @param writes Dependency keys this task writes.
                 * @param local  Local fallback invoked as local(i) for i in [0, n).
                 * @param params Scalar params copied into TaskMetadata (index order).
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Graph& add(::SushiRuntime::Graph::OpID op, std::size_t n,
                           const std::vector<void*>& reads,
                           const std::vector<void*>& writes,
                           F&& local,
                           std::initializer_list<std::uint64_t> params = {})
                {
                    return emit_offload_per_element(op, n,
                        std::vector<Core::ResourceRegion>(reads.begin(), reads.end()),
                        std::vector<Core::ResourceRegion>(writes.begin(), writes.end()),
                        std::forward<F>(local), std::vector<std::uint64_t>(params));
                }

                /**
                 * @brief Adds an offloadable task naming its data with Reads/Writes.
                 *
                 * The ergonomic form of the offload overload: pass the touched
                 * Buffers/States as Reads(...)/Writes(...) instead of building the
                 * dependency-key vectors by hand. Behaviour is otherwise identical —
                 * remote when a willing worker exists, the local fallback otherwise.
                 *
                 * @param op     OpID of the kernel, e.g. "vec.add"_op.
                 * @param n      Iteration range for the local fallback parallel_for.
                 * @param reads  Handles this task reads.
                 * @param writes Handles this task writes.
                 * @param local  Local fallback invoked as local(i) for i in [0, n).
                 * @param params Scalar params copied into TaskMetadata (index order).
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Graph& add(::SushiRuntime::Graph::OpID op, std::size_t n,
                           const Reads& reads, const Writes& writes,
                           F&& local,
                           std::initializer_list<std::uint64_t> params = {})
                {
                    observe(reads);
                    observe(writes);
                    return emit_offload_per_element(op, n, reads.keys, writes.keys,
                                                    std::forward<F>(local),
                                                    std::vector<std::uint64_t>(params));
                }

                /**
                 * @brief Adds an offloadable per-step update rule for a State.
                 *
                 * The distributed counterpart of add(State, fn): the State's step may
                 * ship to a worker (when @p op is registered cluster-wide and a worker
                 * is willing) or run its local fallback, with the same graph correct
                 * either way. Because a State rotates its allocations each step, the
                 * node tracks the step's read/write buffers through live pointer lists
                 * the runtime re-points before every step, so the compiled plan stays
                 * replayable while the offload follows the double-buffer swap. Register
                 * the State with State::distributed() first so both allocations can
                 * cross to a worker.
                 *
                 * @tparam T  Element type of the State.
                 * @param op  OpID of the kernel, e.g. "heat.step"_op.
                 * @param s   The state this rule evolves; must outlive the graph.
                 * @param local Local fallback invoked as local(i, const T* cur, T* next).
                 * @param params Scalar params copied into TaskMetadata (index order).
                 * @return *this, for chaining.
                 */
                template <typename T, class F,
                          std::enable_if_t<std::is_invocable_v<F, std::size_t,
                                                               const T*, T*>, int> = 0>
                Graph& add(::SushiRuntime::Graph::OpID op, State<T>& s, F&& local,
                           std::initializer_list<std::uint64_t> params = {})
                {
                    observe_state(s);
                    auto view = steps_.view_for(s);
                    State<T>* sp = &s;
                    auto live_reads  = std::make_shared<std::vector<void*>>(
                        std::vector<void*>{ s.read_key() });
                    auto live_writes = std::make_shared<std::vector<void*>>(
                        std::vector<void*>{ s.write_key() });
                    std::vector<std::uint64_t> pv(params);

                    specs_.push_back([fn = std::forward<F>(local), view, sp, op, pv,
                                      live_reads, live_writes](BackingGraph& bg)
                    {
                        ::SushiRuntime::Graph::TaskMetadata meta{};
                        meta.name  = "fluent_offload_state";
                        meta.op_id = op;
                        for (std::size_t i = 0; i < pv.size() &&
                             i < ::SushiRuntime::Graph::TaskMetadata::K_MAX_PARAMS; ++i)
                            meta.set_param<std::uint64_t>(static_cast<int>(i), pv[i]);

                        const std::size_t n = sp->size();
                        bg.add_offload_dynamic(meta, live_reads, live_writes,
                            [fn, view, n](sycl::queue& q,
                                          const std::vector<sycl::event>&) -> sycl::event
                            {
                                // Read the live pointers each submit so the local
                                // fallback follows the swap exactly as the offload does.
                                const T* cur = view->cur;
                                T* next = view->next;
                                return q.submit([&](sycl::handler& h)
                                {
                                    h.parallel_for(sycl::range<1>(n),
                                        [fn, cur, next](sycl::id<1> i)
                                        {
                                            fn(i[0], cur, next);
                                        });
                                });
                            });
                    });

                    steps_.register_advance(s, view);

                    // Re-point the live IO at the step's current/next allocations
                    // before each step so the coordinator resolves the right handles.
                    steps_.add_dist_io_updater([view, live_reads, live_writes]()
                    {
                        (*live_reads)[0]  = view->cur;
                        (*live_writes)[0] = view->next;
                    });

                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /** @brief Number of update rules (tasks) added so far. */
                std::size_t size() const noexcept { return task_count_; }

                /**
                 * @brief Number of times the graph has been compiled.
                 *
                 * Exposes the replay guarantee: a graph that only changes late-bound
                 * size or enablement between steps recompiles zero extra times, so
                 * this stays at 1 across a whole run after the first build.
                 * @return The compile count since this Graph was created.
                 */
                std::size_t compile_count() const noexcept { return compiles_; }

                /**
                 * @brief Runs the graph once, blocking until it completes.
                 * @return A RunReport summarising the run.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport run() { return run(std::size_t{1}); }

                /**
                 * @brief Runs @p steps successive steps, blocking until done.
                 *
                 * The graph is compiled on the first run (and after any mutation),
                 * then the compiled plan is replayed for each step: node dependency
                 * state is reset, the plan re-executed, and every State advanced to
                 * its next field. Compilation happens at most once per call.
                 *
                 * @param steps Number of steps to run.
                 * @return A RunReport aggregating the whole run.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport run(std::size_t steps)
                {
                    return unwrap(try_run(steps));
                }

                /**
                 * @brief Runs steps until a wall-clock @p budget elapses.
                 * @param budget Wall-clock limit; at least one step always runs.
                 * @return A RunReport aggregating the steps that ran.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                template <class Rep, class Period>
                Core::RunReport run_for(std::chrono::duration<Rep, Period> budget)
                {
                    const auto deadline = std::chrono::steady_clock::now() + budget;
                    return unwrap(drive([deadline](std::size_t k)
                    {
                        return k == 0 || std::chrono::steady_clock::now() < deadline;
                    }));
                }

                /**
                 * @brief Runs steps until @p done returns true.
                 * @param done Stop condition checked before each step; the run ends
                 *             the first time it returns true.
                 * @return A RunReport aggregating the steps that ran.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                template <class Predicate>
                Core::RunReport run_until(Predicate done)
                {
                    return unwrap(drive([done](std::size_t) { return !done(); }));
                }

                /**
                 * @brief Requests cancellation of an in-progress run, from any thread.
                 *
                 * Safe to call concurrently with a blocking run()/run_for()/run_until()
                 * executing on another thread: the active step drains its remaining
                 * tasks without launching new kernels and returns, the driver stops
                 * before the next step, and the resulting RunReport has cancelled set.
                 * A no-op when no run is active; the flag is cleared when the next run
                 * starts. Cancellation is a clean stop, not an error — it does not throw.
                 */
                void cancel() { ctx_->get_execution_engine().cancel(); }

                /**
                 * @brief Non-throwing single run.
                 * @return The RunReport on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport, Core::Error> try_run() noexcept
                {
                    return try_run(std::size_t{1});
                }

                /**
                 * @brief Non-throwing multi-step run.
                 * @param steps Number of steps to run.
                 * @return The RunReport on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport, Core::Error>
                try_run(std::size_t steps) noexcept
                {
                    return drive([steps](std::size_t k) { return k < steps; });
                }

            private:
                friend class Runtime;
                // A Region IS a recorder: it derives from Graph to inherit every
                // add() overload, so it needs the protected-by-friendship ctor and
                // access to the recorded specs/updaters. The DynamicGraph composer
                // reads those same members to build one combined plan.
                friend class Region;
                friend class DynamicGraph;

                explicit Graph(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx))
                {
                }

                /** @brief Throws the carried error, or returns the report. */
                static Core::RunReport unwrap(Core::Result<Core::RunReport, Core::Error> r)
                {
                    if (!r)
                        throw Core::SushiException(std::move(r.error()));
                    return *r;
                }

                /**
                 * @brief Non-throwing run driver shared by every run/try_run entry.
                 *
                 * @param keep_going Called with the count of completed steps before
                 *                   each step; the loop continues while it is true.
                 * @return The aggregated RunReport, or a mapped Error on failure.
                 */
                template <class KeepGoing>
                Core::Result<Core::RunReport, Core::Error> drive(KeepGoing keep_going) noexcept
                {
                    if (task_count_ == 0)
                        return Core::make_error(Core::Error{Core::Errc::invalid_graph,
                                                            "Graph has no tasks to run."});
                    if (has_dangling_resource())
                        return Core::make_error(Core::Error{Core::Errc::dangling_resource,
                            "Graph references a Buffer/State freed before the run."});
                    try
                    {
                        return drive_impl(keep_going);
                    }
                    catch (const Core::SushiException& e)
                    {
                        return Core::make_error(e.error());
                    }
                    catch (const sycl::exception& e)
                    {
                        return Core::make_error(Core::Error{Core::Errc::kernel_failed, e.what()});
                    }
                    catch (const std::exception& e)
                    {
                        return Core::make_error(Core::Error{Core::Errc::compile_failed, e.what()});
                    }
                    catch (...)
                    {
                        return Core::make_error(Core::Error{Core::Errc::compile_failed,
                                                            "unknown exception"});
                    }
                }

                template <class KeepGoing>
                Core::RunReport drive_impl(KeepGoing keep_going)
                {
                    using clock = std::chrono::steady_clock;
                    auto ms = [](clock::time_point a, clock::time_point b)
                    {
                        return std::chrono::duration<double, std::milli>(b - a).count();
                    };

                    double compile_ms = 0.0;
                    if (dirty_)
                    {
                        const auto c0 = clock::now();
                        build_plan();
                        compile_ms = ms(c0, clock::now());
                    }

                    // Point every State view at its current/next buffers before the
                    // first step. Covers both a fresh build and a replay continuing
                    // from a previous run.
                    steps_.resync();

                    auto& engine = ctx_->get_execution_engine();

                    // Start each run with a clean cancellation flag so a cancel that
                    // ended a previous run does not abort this one.
                    engine.reset_cancellation();

                    // Clear the profiling accumulators so the report covers only this
                    // run; only when the context was built for profiling.
                    const bool profiling = ctx_->profiling_enabled();
                    if (profiling)
                        engine.reset_profiling(plan_);

                    const auto before = engine.metrics();
                    const std::size_t d2h_before = ctx_->device_to_host_bytes();
                    const std::size_t h2d_before = ctx_->host_to_device_bytes();
                    const auto t0 = clock::now();

                    std::size_t executed = 0;
                    while (keep_going(executed) && !engine.is_cancelled())
                    {
                        // Re-point any double-buffered offload node's live IO and
                        // refresh late-bound node controls (size, enablement) for this
                        // step before replaying the compiled plan (no-op otherwise).
                        steps_.before_step();

                        reset_plan_state();
                        engine.execute(plan_); // lvalue -> non-consuming replay

                        // Advance every State after the step so its just-written
                        // next field becomes current — both for the following step
                        // and for the caller reading current() once the run ends.
                        steps_.after_step();
                        ++executed;
                    }

                    const auto t1 = clock::now();
                    const auto after = engine.metrics();

                    const std::size_t total_tasks = task_count_ * executed;
                    auto report = detail::build_run_report(*ctx_, before, after, total_tasks,
                                                           compile_ms, ms(t0, t1));
                    report.cancelled = engine.is_cancelled();

                    // Explicit host<->device transfers issued during this run window
                    // (selective readback). Implicit USM migration is not counted.
                    report.device_to_host_bytes = ctx_->device_to_host_bytes() - d2h_before;
                    report.host_to_device_bytes = ctx_->host_to_device_bytes() - h2d_before;

                    // Per-node and per-worker timing, only when profiling is on.
                    if (profiling)
                        engine.collect_profiling(plan_, report);
                    return report;
                }

                // Appends a capture wrapper's dependency key to the read and/or
                // write list according to the access it declares. An InOut counts
                // toward both, so a read-modify-write task orders correctly.
                template <class Cap>
                static void collect_keys(std::vector<Core::ResourceRegion>& reads,
                                         std::vector<Core::ResourceRegion>& writes, const Cap& c)
                {
                    if (Cap::reads)
                        reads.push_back(c.key);
                    if (Cap::writes)
                        writes.push_back(c.key);
                }

                // Records the liveness observers of the typed handles a task touches,
                // so a run can detect a handle freed before it ran. Raw-pointer and
                // bare-region overloads contribute none — naming data by raw pointer
                // opts out of lifetime tracking, by design.
                void observe(const Reads& r)
                {
                    observers_.insert(observers_.end(), r.lives.begin(), r.lives.end());
                }
                void observe(const Writes& w)
                {
                    observers_.insert(observers_.end(), w.lives.begin(), w.lives.end());
                }
                template <typename T>
                void observe_state(const State<T>& s)
                {
                    Core::LivenessObserver o = s.liveness();
                    if (!o.expired())
                        observers_.push_back(std::move(o));
                }
                template <class Cap>
                void observe_capture(const Cap& c)
                {
                    if (!c.life.expired())
                        observers_.push_back(c.life);
                }

                // True when any recorded handle's allocation has been freed. Checked
                // once per run so a graph outliving its data fails cleanly instead of
                // dispatching against released memory.
                bool has_dangling_resource() const
                {
                    for (const auto& o : observers_)
                        if (o.expired())
                            return true;
                    return false;
                }

                // Emits the node for the variadic coupled-State overload once the
                // trailing callable has been split from the read States. @p ins
                // selects the further read States out of @p tup; @p in0 is the first.
                template <typename T, typename A, class Tuple, std::size_t... Is, class F>
                Graph& emit_coupled(State<T>& out, State<A>& in0, Tuple& tup,
                                    std::index_sequence<Is...>, F&& fn)
                {
                    constexpr std::size_t K = 1 + sizeof...(Is);  // number of read inputs
                    using Seq = std::make_index_sequence<K>;

                    observe_state(out);
                    observe_state(in0);
                    (observe_state(std::get<Is>(tup)), ...);

                    auto ov = steps_.view_for(out);
                    auto views = std::make_tuple(steps_.view_for(in0), steps_.view_for(std::get<Is>(tup))...);
                    auto states = std::make_tuple(&out, &in0, &std::get<Is>(tup)...);
                    State<T>* op = &out;

                    specs_.push_back([fn = std::forward<F>(fn), ov, views, states, op]
                                     (BackingGraph& bg)
                    {
                        const std::size_t n = op->size();
                        auto reads = detail::read_keys_of(states,
                            std::make_index_sequence<K + 1>{});
                        bg.add_node([fn, ov, views, n](sycl::handler& h)
                        {
                            const T* cur = ov->cur;
                            T* next = ov->next;
                            // Resolve the input current pointers host-side at submit
                            // so the device lambda captures raw pointers, not views.
                            auto ins_ptrs = detail::views_current(views, Seq{});
                            h.parallel_for(sycl::range<1>(n), [=](sycl::id<1> i)
                            {
                                detail::invoke_state(fn, i[0], cur, next, ins_ptrs, Seq{});
                            });
                        }, reads, { op->write_key() });
                    });

                    steps_.register_advance(out, ov);
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                // Emits the node for the In/Out/InOut capture overload once the
                // trailing callable has been split from the capture wrappers. @p caps
                // selects the further captures out of @p tup; @p c0 is the first.
                template <int N, class Cap0, class Tuple, std::size_t... Is, class F>
                Graph& emit_captured(Extent<N> e, Cap0 c0, Tuple& tup,
                                     std::index_sequence<Is...>, F&& fn)
                {
                    std::vector<Core::ResourceRegion> reads;
                    std::vector<Core::ResourceRegion> writes;
                    collect_keys(reads, writes, c0);
                    (collect_keys(reads, writes, std::get<Is>(tup)), ...);

                    observe_capture(c0);
                    (observe_capture(std::get<Is>(tup)), ...);

                    auto ptrs = std::make_tuple(c0.ptr, std::get<Is>(tup).ptr...);
                    constexpr std::size_t K = 1 + sizeof...(Is);

                    specs_.push_back([fn = std::forward<F>(fn), e, reads, writes, ptrs]
                                     (BackingGraph& bg)
                    {
                        bg.add_node([fn, e, ptrs](sycl::handler& h)
                        {
                            const sycl::id<N> off = e.offset;
                            h.parallel_for(e.extent, [fn, off, ptrs](sycl::id<N> idx)
                            {
                                detail::invoke_with_pointers(
                                    fn, idx + off, ptrs, std::make_index_sequence<K>{});
                            });
                        }, reads, writes);
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                // Emits a 1-D per-element tracked task. The single funnel for both
                // the raw-pointer and the Reads/Writes explicit-tracking overloads,
                // so the task records exactly one set of read/write regions however
                // the caller named them.
                template <class F>
                Graph& emit_per_element_tracked(std::vector<Core::ResourceRegion> reads,
                                                std::vector<Core::ResourceRegion> writes,
                                                std::size_t n, F&& fn)
                {
                    specs_.push_back([fn = std::forward<F>(fn), n,
                                      reads = std::move(reads),
                                      writes = std::move(writes)](BackingGraph& bg)
                    {
                        bg.add_node([fn, n](sycl::handler& h)
                        {
                            h.parallel_for(sycl::range<1>(n),
                                           [fn](sycl::id<1> i) { fn(i[0]); });
                        }, reads, writes);
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                // Emits a 1-D per-element task whose iteration size and enablement
                // are late-bound through a LateBindings block. The kernel reads the
                // live count at submit (USE_BAKED falls back to capacity), so the
                // node follows per-step size changes without recompiling; the
                // scheduler honours the enabled flag. The dependency regions cover
                // the full capacity, so any live size within it is race-free.
                template <class F>
                Graph& emit_dynamic_per_element(Dynamic dyn,
                                                std::vector<Core::ResourceRegion> reads,
                                                std::vector<Core::ResourceRegion> writes,
                                                std::size_t capacity, F&& fn)
                {
                    auto lb = std::make_shared<::SushiRuntime::Graph::LateBindings>();
                    specs_.push_back([fn = std::forward<F>(fn), capacity, lb,
                                      reads = std::move(reads),
                                      writes = std::move(writes)](BackingGraph& bg)
                    {
                        bg.add_node([fn, capacity, lb](sycl::handler& h)
                        {
                            const std::size_t live =
                                lb->count.load(std::memory_order_relaxed);
                            const std::size_t n =
                                live == ::SushiRuntime::Graph::LateBindings::USE_BAKED
                                    ? capacity : live;
                            h.parallel_for(sycl::range<1>(n),
                                           [fn](sycl::id<1> i) { fn(i[0]); });
                        }, reads, writes, lb);
                    });
                    steps_.register_binding_updater(lb, std::move(dyn));
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                // Emits a 1-D State update rule with late-bound size and enablement.
                // Mirrors emit_dynamic_per_element but reads the State's current/next
                // pointers fresh each submit (following the swap) and registers the
                // once-per-step advance, so a stepped field can vary its active count
                // or sleep entirely with no recompile.
                template <typename T, class F>
                Graph& emit_dynamic_state(Dynamic dyn, State<T>& s, F&& fn)
                {
                    auto view = steps_.view_for(s);
                    State<T>* sp = &s;
                    auto lb = std::make_shared<::SushiRuntime::Graph::LateBindings>();

                    specs_.push_back([fn = std::forward<F>(fn), view, sp, lb](BackingGraph& bg)
                    {
                        const std::size_t capacity = sp->size();
                        bg.add_node([fn, view, lb, capacity](sycl::handler& h)
                        {
                            const T* cur = view->cur;
                            T* next = view->next;
                            const std::size_t live =
                                lb->count.load(std::memory_order_relaxed);
                            const std::size_t n =
                                live == ::SushiRuntime::Graph::LateBindings::USE_BAKED
                                    ? capacity : live;
                            h.parallel_for(sycl::range<1>(n),
                                           [=](sycl::id<1> i) { fn(i[0], cur, next); });
                        }, { sp->read_key() }, { sp->write_key() }, lb);
                    });

                    steps_.register_advance(s, view);
                    steps_.register_binding_updater(lb, std::move(dyn));
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                // Emits a 1-D offloadable per-element task with a local fallback. The
                // single funnel for both the raw-pointer and the Reads/Writes offload
                // overloads. Setting op_id is what makes compile() consider the node
                // offloadable; otherwise the local parallel_for runs.
                template <class F>
                Graph& emit_offload_per_element(::SushiRuntime::Graph::OpID op, std::size_t n,
                                                std::vector<Core::ResourceRegion> reads,
                                                std::vector<Core::ResourceRegion> writes,
                                                F&& local, std::vector<std::uint64_t> pv)
                {
                    specs_.push_back([fn = std::forward<F>(local), n, op,
                                      reads = std::move(reads),
                                      writes = std::move(writes),
                                      pv = std::move(pv)](BackingGraph& bg)
                    {
                        ::SushiRuntime::Graph::TaskMetadata meta{};
                        meta.name   = "fluent_offload";
                        meta.op_id  = op;
                        for (std::size_t i = 0; i < pv.size() &&
                             i < ::SushiRuntime::Graph::TaskMetadata::K_MAX_PARAMS; ++i)
                            meta.set_param<std::uint64_t>(static_cast<int>(i), pv[i]);

                        bg.add_task(meta, reads, writes,
                            [fn, n](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
                            {
                                return q.submit([&](sycl::handler& h)
                                {
                                    h.parallel_for(sycl::range<1>(n),
                                                   [fn](sycl::id<1> i) { fn(i[0]); });
                                });
                            });
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                // Builds a fresh backing graph from the recorded specs, compiles it,
                // and caches the plan. The compiled plan snapshots its own replay
                // baseline (in-degrees) so the fluent layer never touches Node's
                // scheduler-internal state. compile() drains its builder, so a fresh
                // BackingGraph is built whenever the topology changes.
                void build_plan()
                {
                    BackingGraph bg(*ctx_);
                    for (auto& spec : specs_)
                        spec(bg);
                    plan_ = bg.compile();
                    plan_.capture_base_state();
                    dirty_ = false;
                    ++compiles_;
                }

                // Restores the cached plan's per-run state so it can run again. The
                // scheduler re-arms itself; this resets the DAG-side state a previous
                // run consumed, owned by CompiledGraph next to the Node definition.
                void reset_plan_state() noexcept
                {
                    plan_.rearm();
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;

                // Recorded task specifications: each replays one add() onto a fresh
                // backing graph. The fluent Graph is the source of truth so it can
                // rebuild and recompile when mutated.
                std::vector<std::function<void(BackingGraph&)>> specs_;

                // Liveness observers of every typed handle the recorded tasks touch.
                // Checked once per run to catch a Buffer/State freed before the graph
                // ran. Empty for a graph built only from raw-pointer overloads.
                std::vector<Core::LivenessObserver> observers_;

                // The per-step lifecycle: State double-buffer views, advancers,
                // late-binding refreshers, and distributed live-IO re-pointers. Owns
                // the ordering invariant run around each replayed step, shared with
                // the DynamicGraph composer so both drive the loop identically.
                detail::StepMachinery steps_;

                std::size_t task_count_ = 0;

                // Number of times the backing graph has been compiled. A late-bound
                // graph proves its value by holding this at 1 across a multi-step run
                // whose sizes and enable flags change every step.
                std::size_t compiles_ = 0;

                // Cached compiled plan, valid while !dirty_; replayed across steps.
                // It owns its own replay baseline (see capture_base_state/rearm).
                Execution::CompiledGraph plan_;
                bool dirty_ = true;
        };

    } // namespace API
} // namespace SushiRuntime
