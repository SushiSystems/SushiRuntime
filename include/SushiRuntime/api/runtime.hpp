/**************************************************************************/
/* runtime.hpp                                                            */
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
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/buffer.hpp>
#include <SushiRuntime/api/launch.hpp>
#include <SushiRuntime/api/detail/run_report_builder.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/graph/task_types.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    /**
     * @brief The fluent, public v1 API: a Runtime that hands out chainable graphs.
     *
     * This layer sits on top of RuntimeContext / TaskGraph / the execution engine
     * and is the surface users program against. It is header-only and template-
     * based, so it works identically across every supported SYCL toolchain.
     */
    namespace API
    {
        // The lower-level graph builder this layer wraps. Spelled out fully to
        // avoid clashing with the API::Graph class name below.
        using BackingGraph = ::SushiRuntime::Graph::TaskGraph;

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
            // Pulls the read/write dependency key out of either data handle. A
            // Buffer has one base address; a State exposes its current buffer as the
            // read key and its next buffer as the write key. These free functions let
            // the Reads/Writes/In/Out wrappers accept both handle types uniformly.
            template <typename T>
            inline void* read_key_of(const Buffer<T>& b) noexcept { return b.dependency_key(); }
            template <typename T>
            inline void* read_key_of(const State<T>& s) noexcept { return s.read_key(); }
            template <typename T>
            inline void* write_key_of(const Buffer<T>& b) noexcept { return b.dependency_key(); }
            template <typename T>
            inline void* write_key_of(const State<T>& s) noexcept { return s.write_key(); }
        } // namespace detail

        /**
         * @brief Names the data a task reads, as a list of dependency keys.
         *
         * The ergonomic counterpart to a hand-built std::vector<void*>: pass the
         * Buffers and States a task reads and this collects their read keys, so the
         * caller never spells dependency_key()/read_key() by hand. Used by the
         * explicit-tracking and offload add() overloads to order tasks by data.
         */
        struct Reads
        {
            std::vector<void*> keys;  /**< Read dependency keys, in argument order. */

            /** @brief Collects the read key of each handle passed. */
            template <class... Handles>
            explicit Reads(const Handles&... hs) : keys{ detail::read_key_of(hs)... } {}
        };

        /**
         * @brief Names the data a task writes, as a list of dependency keys.
         *
         * As Reads, but gathers write keys (a State contributes its next buffer).
         */
        struct Writes
        {
            std::vector<void*> keys;  /**< Write dependency keys, in argument order. */

            /** @brief Collects the write key of each handle passed. */
            template <class... Handles>
            explicit Writes(const Handles&... hs) : keys{ detail::write_key_of(hs)... } {}
        };

        /**
         * @brief A kernel argument captured for read, with auto dependency tracking.
         *
         * Wraps a Buffer so add() can both register the read and hand the kernel the
         * raw element pointer — the caller writes the data flow once and gets pointer
         * capture and dependency ordering together. @ref ACCESS marks the intent.
         *
         * @tparam T Element type of the wrapped buffer.
         */
        template <typename T>
        struct In
        {
            const T* ptr;  /**< Element pointer handed to the kernel. */
            void* key;     /**< Read dependency key for this argument. */
            static constexpr bool reads = true;   /**< Contributes a read key. */
            static constexpr bool writes = false; /**< Contributes no write key. */

            /** @brief Captures @p b as a read-only kernel argument. */
            In(const Buffer<T>& b) : ptr(b.data()), key(b.dependency_key()) {}
        };

        /**
         * @brief A kernel argument captured for write, with auto dependency tracking.
         * @tparam T Element type of the wrapped buffer.
         */
        template <typename T>
        struct Out
        {
            T* ptr;        /**< Element pointer handed to the kernel. */
            void* key;     /**< Write dependency key for this argument. */
            static constexpr bool reads = false;  /**< Contributes no read key. */
            static constexpr bool writes = true;  /**< Contributes a write key. */

            /** @brief Captures @p b as a write-only kernel argument. */
            Out(Buffer<T>& b) : ptr(b.data()), key(b.dependency_key()) {}
        };

        /**
         * @brief A kernel argument captured for read-modify-write tracking.
         * @tparam T Element type of the wrapped buffer.
         */
        template <typename T>
        struct InOut
        {
            T* ptr;        /**< Element pointer handed to the kernel. */
            void* key;     /**< Dependency key counted as both read and write. */
            static constexpr bool reads = true;   /**< Contributes a read key. */
            static constexpr bool writes = true;  /**< Contributes a write key. */

            /** @brief Captures @p b as a read-and-write kernel argument. */
            InOut(Buffer<T>& b) : ptr(b.data()), key(b.dependency_key()) {}
        };

        // Class-template argument deduction so the wrapper element type follows the
        // buffer: In(buf), Out(buf), InOut(buf) need no <T>.
        template <typename T> In(const Buffer<T>&) -> In<T>;
        template <typename T> Out(Buffer<T>&) -> Out<T>;
        template <typename T> InOut(Buffer<T>&) -> InOut<T>;

        namespace detail
        {
            // True for the In/Out/InOut kernel-argument wrappers. Lets the
            // auto-capture add() overload tell its capture arguments from the
            // trailing callable.
            template <class T> struct is_capture : std::false_type {};
            template <class T> struct is_capture<In<T>>    : std::true_type {};
            template <class T> struct is_capture<Out<T>>   : std::true_type {};
            template <class T> struct is_capture<InOut<T>> : std::true_type {};
            template <class T>
            inline constexpr bool is_capture_v = is_capture<std::decay_t<T>>::value;

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
            inline std::vector<void*> read_keys_of(const StatesTuple& states,
                                                   std::index_sequence<Is...>)
            {
                return std::vector<void*>{ std::get<Is>(states)->read_key()... };
            }
        } // namespace detail

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
                    return add(reads.keys, writes.keys, n, std::forward<F>(fn));
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
                    const std::vector<void*> r = reads.keys;
                    const std::vector<void*> w = writes.keys;
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
                    auto view = view_for(s);
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

                    register_advance(s, view);
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
                    auto view = view_for(s);
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

                    register_advance(s, view);
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
                    specs_.push_back([fn = std::forward<F>(fn), n, reads, writes](BackingGraph& bg)
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
                    std::vector<std::uint64_t> pv(params);
                    specs_.push_back([fn = std::forward<F>(local), n, reads, writes, op, pv]
                                     (BackingGraph& bg)
                    {
                        ::SushiRuntime::Graph::TaskMetadata meta{};
                        meta.name   = "fluent_offload";
                        meta.op_id  = op;
                        for (std::size_t i = 0; i < pv.size() &&
                             i < ::SushiRuntime::Graph::TaskMetadata::K_MAX_PARAMS; ++i)
                            meta.set_param<std::uint64_t>(static_cast<int>(i), pv[i]);

                        // Host-fallback shape (queue + deps -> event): run locally as
                        // a parallel_for when this task is not offloaded. Setting
                        // op_id is what makes compile() consider it offloadable.
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
                    return add(op, n, reads.keys, writes.keys,
                               std::forward<F>(local), params);
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
                    auto view = view_for(s);
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

                    register_advance(s, view);

                    // Re-point the live IO at the step's current/next allocations
                    // before each step so the coordinator resolves the right handles.
                    dist_io_updaters_.push_back([view, live_reads, live_writes]()
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
                    for (auto& resync : view_resyncs_)
                        resync();

                    auto& engine = ctx_->get_execution_engine();
                    const auto before = engine.metrics();
                    const auto t0 = clock::now();

                    std::size_t executed = 0;
                    while (keep_going(executed))
                    {
                        // Re-point any double-buffered offload node's live IO at this
                        // step's allocations before the plan runs (no-op otherwise).
                        for (auto& update_io : dist_io_updaters_)
                            update_io();

                        reset_plan_state();
                        engine.execute(plan_); // lvalue -> non-consuming replay

                        // Advance every State after the step so its just-written
                        // next field becomes current — both for the following step
                        // and for the caller reading current() once the run ends.
                        for (auto& advance : step_advancers_)
                            advance();
                        ++executed;
                    }

                    const auto t1 = clock::now();
                    const auto after = engine.metrics();

                    const std::size_t total_tasks = task_count_ * executed;
                    return detail::build_run_report(*ctx_, before, after, total_tasks,
                                                    compile_ms, ms(t0, t1));
                }

                // Returns the shared view for a State, creating it (and registering
                // its resync) on first use. Every kernel touching a State captures
                // the same view, so one advancer keeps all references current — the
                // basis for correct coupled (multi-field) updates.
                template <typename T>
                std::shared_ptr<detail::StateView<T>> view_for(State<T>& s)
                {
                    const void* key = &s;
                    auto it = state_views_.find(key);
                    if (it != state_views_.end())
                        return std::static_pointer_cast<detail::StateView<T>>(it->second);

                    auto view = std::make_shared<detail::StateView<T>>();
                    state_views_.emplace(key, view);
                    State<T>* sp = &s;
                    view_resyncs_.push_back([sp, view]()
                    {
                        view->cur = sp->current();
                        view->next = sp->next();
                    });
                    return view;
                }

                // Registers the once-per-step advance for a written State. Deduped so
                // a State advances exactly once per step (one update rule per field).
                template <typename T>
                void register_advance(State<T>& s, std::shared_ptr<detail::StateView<T>> view)
                {
                    const void* key = &s;
                    if (!advanced_states_.insert(key).second)
                        return;
                    State<T>* sp = &s;
                    step_advancers_.push_back([sp, view]()
                    {
                        sp->advance();
                        view->cur = sp->current();
                        view->next = sp->next();
                    });
                }

                // Appends a capture wrapper's dependency key to the read and/or
                // write list according to the access it declares. An InOut counts
                // toward both, so a read-modify-write task orders correctly.
                template <class Cap>
                static void collect_keys(std::vector<void*>& reads,
                                         std::vector<void*>& writes, const Cap& c)
                {
                    if (Cap::reads)
                        reads.push_back(c.key);
                    if (Cap::writes)
                        writes.push_back(c.key);
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

                    auto ov = view_for(out);
                    auto views = std::make_tuple(view_for(in0), view_for(std::get<Is>(tup))...);
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

                    register_advance(out, ov);
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
                    std::vector<void*> reads;
                    std::vector<void*> writes;
                    collect_keys(reads, writes, c0);
                    (collect_keys(reads, writes, std::get<Is>(tup)), ...);

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

                // One per State update rule: advances the state and re-points its
                // view at the swapped buffers, run once per step boundary by run(n).
                std::vector<std::function<void()>> step_advancers_;

                // One per double-buffered offload rule: re-points the node's live IO
                // pointer lists at the step's current/next allocations before each
                // step. Empty unless a distributed State step was added.
                std::vector<std::function<void()>> dist_io_updaters_;

                // One shared StateView per State, captured by every kernel that
                // touches it. Type-erased; view_for() casts back to the typed view.
                std::unordered_map<const void*, std::shared_ptr<void>> state_views_;
                // States that already have an advancer (one writer per State).
                std::unordered_set<const void*> advanced_states_;
                // Points each view at its state's current/next; run before each run.
                std::vector<std::function<void()>> view_resyncs_;

                std::size_t task_count_ = 0;

                // Cached compiled plan, valid while !dirty_; replayed across steps.
                // It owns its own replay baseline (see capture_base_state/rearm).
                Execution::CompiledGraph plan_;
                bool dirty_ = true;
        };

#ifdef SR_DISTRIBUTED
        /**
         * @brief Fluent handle to a Master-mode distributed cluster.
         *
         * Returned by Runtime::cluster(), which promotes the runtime to Master and
         * wires the transport. This handle bundles the cluster's tuning knobs and
         * telemetry so callers do not reach into RuntimeContext's distributed_*
         * surface directly. Offloadable tasks are still added with op_id-bearing
         * task metadata; this handle governs the cluster, not the graph.
         */
        class Cluster
        {
            public:
                /**
                 * @brief Blocks until @p n workers have registered, or the timeout.
                 * @return true if the cluster reached @p n live workers in time.
                 */
                bool wait_for_workers(int n, std::chrono::milliseconds timeout)
                {
                    const auto deadline = std::chrono::steady_clock::now() + timeout;
                    while (ctx_->distributed_alive_workers() < n)
                    {
                        if (std::chrono::steady_clock::now() >= deadline)
                            return false;
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    return true;
                }

                /** @brief Live (registered) worker count. */
                int alive_workers() const { return ctx_->distributed_alive_workers(); }

                /** @brief Sets the worker-silence timeout (see RuntimeContext). */
                Cluster& liveness_timeout(std::chrono::milliseconds t)
                {
                    ctx_->distributed_set_liveness_timeout(t);
                    return *this;
                }

                /** @brief Sets the per-task offload deadline (see RuntimeContext). */
                Cluster& task_deadline(std::chrono::milliseconds t)
                {
                    ctx_->distributed_set_task_deadline(t);
                    return *this;
                }

                /** @brief Tasks shipped to workers. */
                std::uint64_t offloaded() const { return ctx_->distributed_offloaded_count(); }
                /** @brief Offloadable tasks the policy kept local. */
                std::uint64_t declined() const { return ctx_->distributed_declined_count(); }
                /** @brief Tasks re-injected after a worker death / deadline / failure. */
                std::uint64_t redispatched() const { return ctx_->distributed_redispatched_count(); }
                /** @brief Writebacks rejected by the version fence. */
                std::uint64_t stale_writebacks() const { return ctx_->distributed_stale_writeback_count(); }

            private:
                friend class Runtime;
                explicit Cluster(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx)) {}
                std::shared_ptr<Execution::RuntimeContext> ctx_;
        };
#endif // SR_DISTRIBUTED

        /**
         * @brief Entry point of the fluent API: owns hardware and hands out graphs.
         *
         * Create one with Runtime::create(), then call graph() to build work.
         * The runtime owns the RuntimeContext (hardware, queues, engine); graphs
         * borrow it, so the Runtime must outlive every graph it produces.
         */
        class Runtime
        {
            public:
                /**
                 * @brief Creates a runtime with default device selection.
                 * @param config Device-class filter passed to hardware discovery.
                 * @return A ready Runtime.
                 */
                static Runtime create(const Topology::HardwareFilterConfig& config = {})
                {
                    return Runtime(std::make_shared<Execution::RuntimeContext>(config));
                }

                /**
                 * @brief Starts a new, empty graph bound to this runtime.
                 * @return A fluent Graph; add tasks, then run() or try_run().
                 */
                Graph graph() { return Graph(ctx_); }

                /**
                 * @brief Allocates a typed shared-USM buffer owned by this runtime.
                 *
                 * The buffer borrows the runtime's context for allocation and
                 * frees itself on destruction, so it must not outlive the Runtime.
                 *
                 * @tparam T            Element type (trivially copyable for device use).
                 * @param count        Number of elements.
                 * @param device_index Target device queue; selects the owning context.
                 * @return An owning Buffer<T> handle.
                 */
                template <typename T>
                Buffer<T> buffer(std::size_t count, std::size_t device_index = 0)
                {
                    return Buffer<T>(ctx_, count, device_index);
                }

                /**
                 * @brief Allocates a double-buffered, time-evolving State field.
                 *
                 * Use this for data a stepped graph reads as "current" and writes
                 * as "next"; the runtime swaps the two each step. Like buffer(),
                 * it must not outlive the Runtime.
                 *
                 * @tparam T            Element type (trivially copyable for device use).
                 * @param count        Number of elements in each of the two buffers.
                 * @param device_index Target device queue for both buffers.
                 * @return An owning State<T> handle.
                 */
                template <typename T>
                State<T> state(std::size_t count, std::size_t device_index = 0)
                {
                    return State<T>(ctx_, count, device_index);
                }

                /** @brief Access to the underlying context for advanced use. */
                Execution::RuntimeContext& context() noexcept { return *ctx_; }

#ifdef SR_DISTRIBUTED
                /**
                 * @brief Promotes this runtime to a distributed Master and returns
                 *        a Cluster handle governing it.
                 *
                 * Thin fluent wrapper over RuntimeContext::enable_distributed_master:
                 * wires the transport and starts serving. Until called, the runtime
                 * is a fully offline single machine (zero distributed overhead).
                 *
                 * @param transport Connected master-side transport (one peer per worker).
                 * @param kernels   Registry shared with this binary's kernels.
                 * @param policy    Offload policy (null → LeastLoadedOffloadPolicy).
                 * @return A Cluster handle for tuning and telemetry.
                 */
                Cluster cluster(std::shared_ptr<Distributed::ITransport> transport,
                                sushi_ptr<Distributed::KernelRegistry> kernels,
                                std::shared_ptr<Distributed::IOffloadPolicy> policy = nullptr)
                {
                    ctx_->enable_distributed_master(std::move(transport),
                                                    std::move(kernels), std::move(policy));
                    return Cluster(ctx_);
                }
#endif // SR_DISTRIBUTED

            private:
                explicit Runtime(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx))
                {
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;
        };

    } // namespace API

    // Promote the entry points into the SushiRuntime namespace so users write
    // `Runtime::create()` after `using namespace SushiRuntime;`. The graph type
    // stays API::Graph (reached through auto) because the name Graph already
    // denotes a namespace at this scope.
    using API::Runtime;
    using API::Reads;
    using API::Writes;
    using API::In;
    using API::Out;
    using API::InOut;
    using Core::Error;
    using Core::Errc;
    using Core::RunReport;
    using Core::SushiException;

} // namespace SushiRuntime
