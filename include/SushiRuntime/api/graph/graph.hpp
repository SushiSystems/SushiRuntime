/**************************************************************************/
/* graph.hpp                                                              */
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
 * @file graph.hpp
 * @brief Declares Graph, the fluent builder that records tasks and runs them.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <string>
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
#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/api/detail/fixed_order_reduce.hpp>
#include <SushiRuntime/api/detail/fixed_order_scan.hpp>
#include <SushiRuntime/api/detail/step_machinery.hpp>
#include <SushiRuntime/api/graph/run_handle.hpp>
#include <SushiRuntime/api/vocabulary/dependencies.hpp>
#include <SushiRuntime/api/vocabulary/captures.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/resource_liveness.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/chain_plan.hpp>
#include <SushiRuntime/execution/chain_run.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /** @brief Names the lower-level graph builder this layer wraps. */
        using BackingGraph = ::SushiRuntime::Graph::TaskGraph;

        /** @brief A graph emitter that records nodes on a backing graph: void(BackingGraph&). */
        template <class F>
        inline constexpr bool is_graph_emitter_v = std::is_invocable_v<F, BackingGraph&>;

        // Forward-declared so that Graph can befriend the region type and its composer.
        class Region;
        class DynamicGraph;

        namespace detail
        {
            // Test-only friend seam onto Graph::plan_ (Graph has no public accessor).
            struct RegistrationIndexTestAccess;
            // Test-only friend seam onto Graph::plan_, for G1 node-event-record tests.
            struct NodeEventRecordsTestAccess;
        }

        /**
         * @brief Holds a run count resolved once per run from exactly one live source.
         * @see include/SushiRuntime/api/graph/README.md
         */
        struct LateCount
        {
            std::size_t capacity = 0; /**< Plan size; the live count must not exceed it. */

            /** @brief Polled once per run when set; overrides capacity as the count. */
            std::function<std::size_t()> host;

            /**
             * @brief Residency::Host slot a predecessor wrote the live count into.
             *
             * Borrowed, not owned: this LateCount only observes it, so the buffer it
             * points to must outlive every graph this LateCount is passed to.
             */
            const Buffer<std::uint32_t>* device = nullptr;

            /** @brief Element index of the live count within @c device. */
            std::size_t device_index = 0;
        };

        namespace detail
        {
            /**
             * @brief Reads a LateCount's live value and checks it against its capacity.
             * @throws Core::SushiException With invalid_access if @c lc.device is set
             *        but is not Residency::Host, or invalid_graph if the live count
             *        exceeds @c lc.capacity, the same way an out-of-window based_at()
             *        fails the run.
             * @see include/SushiRuntime/api/graph/README.md
             */
            inline std::size_t resolve_late_count(const LateCount& lc)
            {
                std::size_t live = lc.capacity;
                if (lc.host)
                {
                    live = lc.host();
                }
                else if (lc.device != nullptr)
                {
                    if (lc.device->residency() != Residency::Host)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                            "LateCount: the device slot is Residency::" +
                            std::string(residency_name(lc.device->residency())) +
                            ", but the live count is read on the host while kernels "
                            "may run; allocate it with Residency::Host."});
                    if (lc.device_index >= lc.device->size())
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                            "LateCount: device_index " + std::to_string(lc.device_index) +
                            " is out of range for a buffer of " +
                            std::to_string(lc.device->size()) + " elements."});
                    live = static_cast<std::size_t>(lc.device->data()[lc.device_index]);
                }

                if (live > lc.capacity)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "LateCount: the live count (" + std::to_string(live) +
                        ") exceeds the capacity it was planned for (" +
                        std::to_string(lc.capacity) + ")."});
                return live;
            }

            /**
             * @brief Rejects an ill-formed LateCount at registration, before add() changes the
             *        graph.
             * @throws Core::SushiException With invalid_access for a non-host
             *        device slot, or invalid_graph for an oversized capacity.
             * @see include/SushiRuntime/api/graph/README.md
             */
            inline void validate_late_count(const LateCount& lc, const char* what)
            {
                if (lc.capacity > static_cast<std::size_t>(
                        std::numeric_limits<std::uint32_t>::max()))
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        std::string(what) + ": capacity (" + std::to_string(lc.capacity) +
                        ") exceeds what a uint32 live-count slot can hold (" +
                        std::to_string(std::numeric_limits<std::uint32_t>::max()) + ")."});
                if (lc.device != nullptr && lc.device->residency() != Residency::Host)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                        std::string(what) + ": the device slot is Residency::" +
                        std::string(residency_name(lc.device->residency())) +
                        ", but the live count is read while kernels may run; "
                        "allocate it with Residency::Host."});
            }
        }

        namespace detail
        {
            /**
             * @brief Names where one LateCount's live value comes from, in a form a kernel carries.
             */
            struct LiveCountSource
            {
                const std::uint32_t* slot = nullptr; /**< Device-written word, or null for a host value. */
                std::size_t host_live = 0;           /**< The live count when @c slot is null. */
                std::size_t capacity = 0;            /**< Bound a device value must not exceed. */
            };

            /**
             * @brief Resolves a LateCount's host side and names its device slot without reading it.
             * @param lc The count to resolve.
             * @return The source a resolver kernel reads.
             * @throws Core::SushiException As resolve_late_count(), or invalid_access for a device index out of range.
             */
            inline LiveCountSource late_count_source(const LateCount& lc)
            {
                LiveCountSource source;
                source.capacity = lc.capacity;
                if (lc.host || lc.device == nullptr)
                {
                    source.host_live = resolve_late_count(lc);
                    return source;
                }
                if (lc.device_index >= lc.device->size())
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                        "LateCount: device_index " + std::to_string(lc.device_index) +
                        " is out of range for a buffer of " +
                        std::to_string(lc.device->size()) + " elements."});
                source.slot = lc.device->data() + lc.device_index;
                return source;
            }

            /**
             * @brief Reads a live count on the device; a value over capacity records a fault and yields zero.
             * @param source Where the count lives, from late_count_source().
             * @param fault  The resolver's fault record; null only when @p source has no slot.
             * @return The live count, never above @p source.capacity.
             */
            inline std::size_t read_live_count(const LiveCountSource& source,
                                               std::uint32_t* fault) noexcept
            {
                if (source.slot == nullptr)
                    return source.host_live;
                const std::size_t live = static_cast<std::size_t>(*source.slot);
                if (live <= source.capacity)
                    return live;
                record_device_fault(fault, DEVICE_FAULT_LATE_COUNT, 0, live, source.capacity);
                return 0;
            }
        }

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
                /**
                 * @brief Takes over another graph's recorded tasks.
                 * @param other Graph to move from; left with no tasks.
                 */
                Graph(Graph&& other) = default;

                /**
                 * @brief Replaces this graph's contents with another's.
                 * @param other Graph to move from; left with no tasks.
                 * @return *this, now owning @p other's tasks.
                 */
                Graph& operator=(Graph&& other) = default;

                Graph(const Graph&) = delete;
                Graph& operator=(const Graph&) = delete;

                /**
                 * @brief Adds a per-element task that records no regions and is ordered against
                 *        nothing.
                 * @param fn Callable invoked as fn(i) for each index i.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Graph& add_untracked(std::size_t n, F&& fn)
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
                 * @brief Adds a range task that records no regions; fn receives bounds and handler.
                 * @param n Upper bound; fn is called as fn(0, n, handler).
                 */
                template <class F, std::enable_if_t<is_range_kernel_v<F>, int> = 0>
                Graph& add_untracked(std::size_t n, F&& fn)
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
                 * @brief Adds a raw kernel that records no regions and drives the handler itself.
                 * @param fn Callable invoked as fn(sycl::handler&).
                 */
                template <class F, std::enable_if_t<is_raw_kernel_v<F>, int> = 0>
                Graph& add_untracked(F&& fn)
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
                 * @brief Adds an emitter that records its own nodes on the backing graph.
                 * @param emit Callable invoked as emit(BackingGraph&) at every build of the plan.
                 * @pre emit only adds nodes: it does not compile, drain or keep the graph.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_graph_emitter_v<F>, int> = 0>
                Graph& add_emitter(F&& emit)
                {
                    specs_.push_back(NodeSpec(std::forward<F>(emit)));
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a multidimensional task over an Extent that records no regions.
                 * @tparam N Iteration rank (1, 2, or 3).
                 * @param fn Callable invoked as fn(sycl::id<N>) per work-item.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <int N, class F,
                          std::enable_if_t<std::is_invocable_v<F, sycl::id<N>>, int> = 0>
                Graph& add_untracked(Extent<N> e, F&& fn)
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
                 * @brief Adds a tiled task over an NdRange that records no regions.
                 * @tparam N Iteration rank (1, 2, or 3).
                 * @param r Global and local ranges; global must tile by local.
                 * @param fn Callable invoked as fn(sycl::nd_item<N>) per work-item.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <int N, class F,
                          std::enable_if_t<std::is_invocable_v<F, sycl::nd_item<N>>, int> = 0>
                Graph& add_untracked(NdRange<N> r, F&& fn)
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
                 * @brief Adds a 1-D per-element task ordered by the handles in Reads and Writes.
                 * @param n Iteration range; fn is called for each i in [0, n).
                 * @see include/SushiRuntime/api/graph/README.md
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
                 * @brief Adds an N-D per-element task over an Extent, ordered by Reads and Writes.
                 * @tparam N Iteration rank (1, 2, or 3).
                 * @param fn Callable invoked as fn(sycl::id<N>) per work-item.
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
                 * @brief Adds a tiled NdRange task ordered by the regions it declares.
                 * @tparam N Iteration rank (1, 2, or 3).
                 * @param r Global and local ranges; global must tile by local.
                 * @param fn Callable invoked as fn(sycl::nd_item<N>) per work-item.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <int N, class F,
                          std::enable_if_t<std::is_invocable_v<F, sycl::nd_item<N>>, int> = 0>
                Graph& add(const Reads& reads, const Writes& writes,
                           NdRange<N> r, F&& fn)
                {
                    observe(reads);
                    observe(writes);
                    const std::vector<Core::ResourceRegion> rk = reads.keys;
                    const std::vector<Core::ResourceRegion> wk = writes.keys;
                    specs_.push_back([fn = std::forward<F>(fn), r, rk, wk](BackingGraph& bg)
                    {
                        bg.add_node([fn, r](sycl::handler& h)
                        {
                            h.parallel_for(r.to_nd_range(),
                                           [fn](sycl::nd_item<N> it) { fn(it); });
                        }, rk, wk);
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a range task ordered by its declared regions; fn drives the handler
                 *        over [0, n).
                 * @param fn Callable invoked as fn(begin, end, sycl::handler&).
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_range_kernel_v<F>, int> = 0>
                Graph& add(const Reads& reads, const Writes& writes,
                           std::size_t n, F&& fn)
                {
                    observe(reads);
                    observe(writes);
                    const std::vector<Core::ResourceRegion> rk = reads.keys;
                    const std::vector<Core::ResourceRegion> wk = writes.keys;
                    specs_.push_back([fn = std::forward<F>(fn), n, rk, wk](BackingGraph& bg)
                    {
                        bg.add_node([fn, n](sycl::handler& h)
                        {
                            fn(std::size_t{0}, n, h);
                        }, rk, wk);
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a raw kernel ordered by its declared regions; fn owns the whole
                 *        command group.
                 * @param fn Callable invoked as fn(sycl::handler&).
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_raw_kernel_v<F>, int> = 0>
                Graph& add(const Reads& reads, const Writes& writes, F&& fn)
                {
                    observe(reads);
                    observe(writes);
                    const std::vector<Core::ResourceRegion> rk = reads.keys;
                    const std::vector<Core::ResourceRegion> wk = writes.keys;
                    specs_.push_back([fn = std::forward<F>(fn), rk, wk](BackingGraph& bg)
                    {
                        bg.add_node(fn, rk, wk);
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds an N-D per-element task that registers its captures and passes their
                 *        pointers.
                 * @tparam N Iteration rank (1, 2, or 3).
                 * @param args Remaining captured arguments, with the callable last.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <int N, class Cap0, class... Args,
                          std::enable_if_t<detail::is_capture_v<Cap0> &&
                                           (sizeof...(Args) >= 1), int> = 0>
                Graph& add(Extent<N> e, Cap0 c0, Args&&... args)
                {
                    // The last element of Args is the callable; the others are captures.
                    auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
                    constexpr std::size_t M = sizeof...(Args);
                    return emit_captured(e, c0, tup, std::make_index_sequence<M - 1>{},
                                         std::get<M - 1>(tup));
                }

                /**
                 * @brief Adds the per-step update rule of a State: reads current, writes next.
                 * @param s The state this rule evolves; must outlive the graph.
                 * @param fn Callable invoked as fn(i, const T* cur, T* next) for each i in [0,
                 *        s.size()).
                 * @see include/SushiRuntime/api/graph/README.md
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
                 * @brief Adds a multidimensional per-step update rule for a State over an Extent.
                 * @param s The state this rule evolves; must outlive the graph.
                 * @param fn Callable invoked as fn(sycl::id<N>, const T* cur, T* next).
                 * @see include/SushiRuntime/api/graph/README.md
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
                 * @brief Adds a coupled update rule that writes out's next field from several
                 *        current fields.
                 * @param out The field this rule writes; must outlive the graph.
                 * @param in0 First read field; must outlive the graph.
                 * @param rest Further read fields, then the callable fn(i, cur, next, a, ...) last.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <typename T, typename A, class... Rest,
                          std::enable_if_t<(sizeof...(Rest) >= 1), int> = 0>
                Graph& add(State<T>& out, State<A>& in0, Rest&&... rest)
                {
                    // The last element of Rest is the callable; the others are read States.
                    auto tup = std::forward_as_tuple(std::forward<Rest>(rest)...);
                    constexpr std::size_t M = sizeof...(Rest);
                    return emit_coupled(out, in0, tup,
                                        std::make_index_sequence<M - 1>{},
                                        std::get<M - 1>(tup));
                }

                /**
                 * @brief Adds a per-element task ordered by the raw base pointers it reads and
                 *        writes.
                 * @param n Iteration range; fn is called for each i in [0, n).
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Graph& add(const std::vector<void*>& reads,
                           const std::vector<void*>& writes,
                           std::size_t n, F&& fn)
                {
                    // A bare base pointer names the whole-buffer region.
                    return emit_per_element_tracked(
                        std::vector<Core::ResourceRegion>(reads.begin(), reads.end()),
                        std::vector<Core::ResourceRegion>(writes.begin(), writes.end()),
                        n, std::forward<F>(fn));
                }

                /**
                 * @brief Adds a late-bound per-element task whose size and enablement vary per
                 *        step.
                 * @param capacity Pre-sized upper bound; the live size must not exceed it.
                 * @see include/SushiRuntime/api/graph/README.md
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
                 * @brief Adds a late-bound per-element task that names its data by raw base
                 *        pointer.
                 * @param capacity Pre-sized upper bound; the live size must not exceed it.
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
                 * @param s The state this rule evolves; must outlive the graph.
                 * @param fn Callable invoked as fn(i, const T* cur, T* next).
                 * @see include/SushiRuntime/api/graph/README.md
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
                 * @param fn Nullary callable run once; must not submit SYCL work.
                 * @see include/SushiRuntime/api/graph/README.md
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
                 * @param fn Nullary callable run once per step; must not submit SYCL work.
                 * @see include/SushiRuntime/api/graph/README.md
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
                 * @brief Names the task most recently added to this graph.
                 * @param name Persistent label, stored not copied; ignored when no task has been
                 *        added.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                Graph& name_last(const char* name) noexcept
                {
                    if (!specs_.empty())
                        specs_.back().name = name;
                    return *this;
                }

                /**
                 * @brief Adds a reduction whose combination order never varies.
                 * @tparam Combine Binary functor; must be trivially copyable.
                 * @param input Values to reduce; must hold at least n elements.
                 * @throws Core::SushiException If @p n exceeds @p input's size, if
                 *        @p output is empty, or if scratch cannot be allocated.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <typename T, class Combine>
                Graph& add_reduce(const Buffer<T>& input, const Buffer<T>& output,
                                  std::size_t n, Combine op, T identity)
                {
                    if (n > input.size())
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_reduce: n (" + std::to_string(n) +
                            ") exceeds the input buffer's size (" +
                            std::to_string(input.size()) + ")."});
                    if (output.size() == 0)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_reduce: the output buffer holds no elements."});

                    T* out_base = output.data();
                    if (n == 0)
                    {
                        // An empty reduction still emits a node, so readers order after it.
                        const Reads reads;
                        const Writes writes(output);
                        // Replicates add()'s liveness-observe step; see emit_per_element_tracked().
                        observe(reads);
                        observe(writes);
                        return emit_per_element_tracked(reads.keys, writes.keys, std::size_t{1},
                                   [out_base, identity](std::size_t)
                                   {
                                       out_base[0] = identity;
                                   },
                                   nullptr, std::size_t{1});
                    }

                    const std::vector<detail::ReduceLevel> levels = detail::plan_reduction(n);
                    const std::size_t scratch_count = detail::reduce_scratch_count(levels);

                    T* scratch = nullptr;
                    if (scratch_count != 0)
                    {
                        std::shared_ptr<T> owned = detail::make_reduce_scratch<T>(
                            ctx_, scratch_count, input.device_index(), input.residency());
                        scratch = owned.get();
                        reduce_scratch_.push_back(std::static_pointer_cast<void>(owned));
                    }

                    const T* in_base = input.data();
                    void* scratch_key = static_cast<void*>(scratch);
                    constexpr std::size_t tile = detail::REDUCE_TILE;

                    // One token shared by every level; the first reserve fills it in.
                    auto registration_token = std::make_shared<std::optional<std::uint32_t>>();
                    reduce_registration_tokens_.push_back(registration_token);

                    for (std::size_t i = 0; i < levels.size(); ++i)
                    {
                        const detail::ReduceLevel& level = levels[i];
                        const bool first = (i == 0);
                        const bool last  = (i + 1 == levels.size());

                        const T* src = first ? in_base : (scratch + level.in_offset);
                        T* dst = last ? out_base : (scratch + level.out_offset);
                        const std::size_t in_count = level.in_count;

                        const Reads reads = first
                            ? Reads(input)
                            : Reads(Core::ResourceRegion{scratch_key,
                                                         level.in_offset * sizeof(T),
                                                         level.in_count * sizeof(T)});
                        const Writes writes = last
                            ? Writes(output)
                            : Writes(Core::ResourceRegion{scratch_key,
                                                          level.out_offset * sizeof(T),
                                                          level.out_count * sizeof(T)});

                        // Replicates add()'s liveness-observe step; see emit_per_element_tracked().
                        observe(reads);
                        observe(writes);
                        emit_per_element_tracked(reads.keys, writes.keys, level.out_count,
                            [src, dst, in_count, op](std::size_t t)
                            {
                                const std::size_t begin = t * tile;
                                std::size_t end = begin + tile;
                                if (end > in_count) end = in_count;
                                dst[t] = detail::fold_tile(op, src, begin, end);
                            },
                            registration_token, level.out_count);
                    }
                    return *this;
                }

                /**
                 * @brief Adds a fixed-order reduction using the combiner's static identity().
                 * @throws Core::SushiException As the five-argument overload.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <typename T, class Combine>
                Graph& add_reduce(const Buffer<T>& input, const Buffer<T>& output,
                                  std::size_t n, Combine op)
                {
                    return add_reduce(input, output, n, op, Combine::identity());
                }

                /**
                 * @brief Adds a reduction whose live element count is late-bound.
                 * @param input Values to reduce; must hold at least n.capacity elements.
                 * @throws Core::SushiException If @p n.capacity exceeds @p input's
                 *        size, if @p output is empty, or at run if the live count
                 *        exceeds @p n.capacity.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <typename T, class Combine>
                Graph& add_reduce(const Buffer<T>& input, const Buffer<T>& output,
                                  LateCount n, Combine op, T identity)
                {
                    detail::validate_late_count(n, "add_reduce");
                    if (n.capacity > input.size())
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_reduce: n.capacity (" + std::to_string(n.capacity) +
                            ") exceeds the input buffer's size (" +
                            std::to_string(input.size()) + ")."});
                    if (output.size() == 0)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_reduce: the output buffer holds no elements."});

                    const std::vector<detail::ReduceLevel> levels =
                        detail::plan_tiled_levels(n.capacity);
                    T* out_base = output.data();

                    if (levels.empty())
                    {
                        // With capacity zero the identity is the answer on every run.
                        return add(Reads(), Writes(output), std::size_t{1},
                                   [out_base, identity](std::size_t)
                                   {
                                       out_base[0] = identity;
                                   });
                    }

                    const std::size_t scratch_count = detail::reduce_scratch_count(levels);
                    T* scratch = nullptr;
                    if (scratch_count != 0)
                    {
                        std::shared_ptr<T> owned = detail::make_reduce_scratch<T>(
                            ctx_, scratch_count, input.device_index(), input.residency());
                        scratch = owned.get();
                        reduce_scratch_.push_back(std::static_pointer_cast<void>(owned));
                    }

                    // state[i] is level i's live in_count, state[level_count+i] its live out_count.
                    const std::size_t level_count = levels.size();
                    std::shared_ptr<std::uint32_t> state = detail::make_reduce_scratch<std::uint32_t>(
                        ctx_, 2 * level_count, input.device_index(), input.residency());
                    reduce_scratch_.push_back(std::static_pointer_cast<void>(state));
                    std::uint32_t* state_ptr = state.get();

                    // One token is shared by every node this call emits.
                    auto registration_token = std::make_shared<std::optional<std::uint32_t>>();
                    reduce_registration_tokens_.push_back(registration_token);

                    const Core::ResourceRegion state_region{state_ptr, 0,
                                                             2 * level_count * sizeof(std::uint32_t)};

                    emit_late_count_resolver(std::array<LateCount, 1>{n}, input.device_index(),
                        Writes(state_region),
                        [state_ptr, level_count](const std::array<std::size_t, 1>& live)
                        {
                            detail::fill_live_level_words(level_count, live[0], state_ptr);
                        },
                        registration_token, std::size_t{1});

                    const T* in_base = input.data();
                    void* scratch_key = static_cast<void*>(scratch);
                    constexpr std::size_t tile = detail::REDUCE_TILE;

                    for (std::size_t i = 0; i < levels.size(); ++i)
                    {
                        const detail::ReduceLevel& level = levels[i];
                        const bool first = (i == 0);
                        const bool last  = (i + 1 == levels.size());

                        const T* src = first ? in_base : (scratch + level.in_offset);
                        T* dst = last ? out_base : (scratch + level.out_offset);

                        Reads reads = first
                            ? Reads(input, state_region)
                            : Reads(Core::ResourceRegion{scratch_key,
                                                         level.in_offset * sizeof(T),
                                                         level.in_count * sizeof(T)},
                                    state_region);
                        Writes writes = last
                            ? Writes(output)
                            : Writes(Core::ResourceRegion{scratch_key,
                                                          level.out_offset * sizeof(T),
                                                          level.out_count * sizeof(T)});

                        observe(reads);
                        observe(writes);
                        emit_device_sized(reads.keys, writes.keys, level.out_count,
                            state_ptr + level_count + i, state_ptr + i,
                            [src, dst, op](std::size_t t, std::size_t in_count)
                            {
                                const std::size_t begin = t * tile;
                                std::size_t end = begin + tile;
                                if (end > in_count) end = in_count;
                                dst[t] = detail::fold_tile(op, src, begin, end);
                            },
                            registration_token, level.out_count);
                    }

                    // A live count of zero folds no level, so this is the identity's only writer.
                    Reads zero_reads = Reads(state_region);
                    Writes zero_writes = Writes(output);
                    observe(zero_reads);
                    observe(zero_writes);
                    emit_device_sized(zero_reads.keys, zero_writes.keys, std::size_t{1},
                        nullptr, state_ptr,
                        [out_base, identity](std::size_t, std::size_t live_n)
                        {
                            if (live_n == 0)
                                out_base[0] = identity;
                        },
                        registration_token, std::size_t{1});
                    return *this;
                }

                /**
                 * @brief Adds a late-bound reduction using the combiner's static identity().
                 * @param input Values to reduce; must hold at least n.capacity elements.
                 * @throws Core::SushiException As the five-argument overload.
                 */
                template <typename T, class Combine>
                Graph& add_reduce(const Buffer<T>& input, const Buffer<T>& output,
                                  LateCount n, Combine op)
                {
                    return add_reduce(input, output, n, op, Combine::identity());
                }

                /**
                 * @brief Reduces each segment of a ragged array in a fixed order.
                 * @param segment_offsets segment_count + 1 non-decreasing entries, the last within
                 *        values.
                 * @throws Core::SushiException If segment_offsets or output is too small for
                 *         segment_count.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <typename T, class Combine>
                Graph& add_segmented_reduce(const Buffer<T>& values,
                                            const Buffer<std::uint32_t>& segment_offsets,
                                            const Buffer<T>& output,
                                            std::size_t segment_count,
                                            Combine op, T identity)
                {
                    if (segment_offsets.size() < segment_count + 1)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_segmented_reduce: segment_offsets holds " +
                            std::to_string(segment_offsets.size()) + " entries but " +
                            std::to_string(segment_count + 1) + " are required."});
                    if (output.size() < segment_count)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_segmented_reduce: output holds " +
                            std::to_string(output.size()) + " entries but " +
                            std::to_string(segment_count) + " are required."});

                    const T* value_base = values.data();
                    const std::uint32_t* offset_base = segment_offsets.data();
                    T* out_base = output.data();

                    const Reads reads(values, segment_offsets);
                    const Writes writes(output);
                    // Replicates add()'s liveness-observe step; see emit_per_element_tracked().
                    observe(reads);
                    observe(writes);
                    return emit_per_element_tracked(reads.keys, writes.keys, segment_count,
                               [value_base, offset_base, out_base, op, identity](std::size_t s)
                               {
                                   const std::size_t begin = offset_base[s];
                                   const std::size_t end = offset_base[s + 1];
                                   out_base[s] = (end > begin)
                                       ? detail::fold_tile(op, value_base, begin, end)
                                       : identity;
                               },
                               nullptr, segment_count);
                }

                /**
                 * @brief Reduces each segment using the combiner's static identity().
                 * @param segment_offsets segment_count + 1 non-decreasing entries.
                 * @param output Holds segment_count entries.
                 * @throws Core::SushiException As the six-argument overload.
                 */
                template <typename T, class Combine>
                Graph& add_segmented_reduce(const Buffer<T>& values,
                                            const Buffer<std::uint32_t>& segment_offsets,
                                            const Buffer<T>& output,
                                            std::size_t segment_count, Combine op)
                {
                    return add_segmented_reduce(values, segment_offsets, output,
                                                segment_count, op, Combine::identity());
                }

                /**
                 * @brief Reduces each segment with a late-bound segment count.
                 * @param segment_offsets segment_count.capacity + 1 non-decreasing entries.
                 * @param output Holds segment_count.capacity entries.
                 * @throws Core::SushiException If segment_offsets or output is too small for
                 *        segment_count.capacity, or at run if the live count exceeds it.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <typename T, class Combine>
                Graph& add_segmented_reduce(const Buffer<T>& values,
                                            const Buffer<std::uint32_t>& segment_offsets,
                                            const Buffer<T>& output,
                                            LateCount segment_count,
                                            Combine op, T identity)
                {
                    if (segment_offsets.size() < segment_count.capacity + 1)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_segmented_reduce: segment_offsets holds " +
                            std::to_string(segment_offsets.size()) + " entries but " +
                            std::to_string(segment_count.capacity + 1) + " are required."});
                    if (output.size() < segment_count.capacity)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_segmented_reduce: output holds " +
                            std::to_string(output.size()) + " entries but " +
                            std::to_string(segment_count.capacity) + " are required."});

                    detail::validate_late_count(segment_count, "add_segmented_reduce");

                    const T* value_base = values.data();
                    const std::uint32_t* offset_base = segment_offsets.data();
                    T* out_base = output.data();
                    const LateCount lc = segment_count;

                    // Same shape as the late-bound add_reduce, one live word instead of a level chain.
                    std::shared_ptr<std::uint32_t> state =
                        detail::make_reduce_scratch<std::uint32_t>(ctx_, 1, values.device_index(),
                                                                    values.residency());
                    reduce_scratch_.push_back(std::static_pointer_cast<void>(state));
                    std::uint32_t* state_ptr = state.get();
                    const Core::ResourceRegion state_region{state_ptr, 0, sizeof(std::uint32_t)};

                    auto registration_token = std::make_shared<std::optional<std::uint32_t>>();
                    reduce_registration_tokens_.push_back(registration_token);

                    emit_late_count_resolver(std::array<LateCount, 1>{lc}, values.device_index(),
                        Writes(state_region),
                        [state_ptr](const std::array<std::size_t, 1>& live)
                        {
                            state_ptr[0] = static_cast<std::uint32_t>(live[0]);
                        },
                        registration_token, std::size_t{1});

                    Reads reads = Reads(values, segment_offsets, state_region);
                    Writes writes = Writes(output);
                    observe(reads);
                    observe(writes);
                    emit_device_sized(reads.keys, writes.keys, segment_count.capacity,
                        state_ptr, nullptr,
                        [value_base, offset_base, out_base, op, identity](std::size_t s)
                        {
                            const std::size_t begin = offset_base[s];
                            const std::size_t end = offset_base[s + 1];
                            out_base[s] = (end > begin)
                                ? detail::fold_tile(op, value_base, begin, end)
                                : identity;
                        },
                        registration_token, segment_count.capacity);
                    return *this;
                }

                /**
                 * @brief Reduces each segment, late-bound, using the combiner's static identity().
                 * @param segment_offsets segment_count.capacity + 1 non-decreasing entries.
                 * @param output Holds segment_count.capacity entries.
                 * @throws Core::SushiException As the late-bound six-argument overload.
                 */
                template <typename T, class Combine>
                Graph& add_segmented_reduce(const Buffer<T>& values,
                                            const Buffer<std::uint32_t>& segment_offsets,
                                            const Buffer<T>& output,
                                            LateCount segment_count, Combine op)
                {
                    return add_segmented_reduce(values, segment_offsets, output,
                                                segment_count, op, Combine::identity());
                }

                /**
                 * @brief Scans uint32 counts into exclusive running offsets, late-bound.
                 * @param offsets Destination; must hold at least n.capacity + 1 elements.
                 * @throws Core::SushiException If a buffer is too small, if @p counts
                 *        and @p offsets sit on different devices, or at run if the
                 *        live count exceeds @p n.capacity.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                Graph& add_prefix_scan(const Buffer<std::uint32_t>& counts,
                                       const Buffer<std::uint32_t>& offsets,
                                       const Buffer<std::uint32_t>& total,
                                       LateCount n)
                {
                    detail::validate_late_count(n, "add_prefix_scan");
                    if (n.capacity > counts.size())
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_prefix_scan: n.capacity (" + std::to_string(n.capacity) +
                            ") exceeds the counts buffer's size (" +
                            std::to_string(counts.size()) + ")."});
                    if (offsets.size() < n.capacity + 1)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_prefix_scan: offsets holds " +
                            std::to_string(offsets.size()) + " elements but " +
                            std::to_string(n.capacity + 1) + " are required."});
                    if (total.size() == 0)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_prefix_scan: the total buffer holds no elements."});
                    if (offsets.device_index() != counts.device_index())
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_prefix_scan: counts and offsets must live on the same device."});

                    const std::vector<detail::ReduceLevel> levels =
                        detail::plan_tiled_levels(n.capacity);
                    std::uint32_t* counts_base = counts.data();
                    std::uint32_t* offsets_base = offsets.data();
                    std::uint32_t* total_base = total.data();

                    if (levels.empty())
                    {
                        // Capacity zero: the resolver checks the live count and writes the scan of nothing.
                        auto zero_token = std::make_shared<std::optional<std::uint32_t>>();
                        reduce_registration_tokens_.push_back(zero_token);
                        emit_late_count_resolver(std::array<LateCount, 1>{n}, counts.device_index(),
                            Writes(offsets, total),
                            [offsets_base, total_base](const std::array<std::size_t, 1>&)
                            {
                                offsets_base[0] = 0;
                                total_base[0] = 0;
                            },
                            zero_token, std::size_t{1});
                        return *this;
                    }

                    const std::size_t level_count = levels.size();
                    const std::size_t up_scratch_count = detail::reduce_scratch_count(levels);
                    const std::size_t down_scratch_count = detail::reduce_scratch_count(levels);

                    // A single level needs no scratch; a zero-size allocation would fail.
                    std::uint32_t* up_scratch = nullptr;
                    if (up_scratch_count != 0)
                    {
                        std::shared_ptr<std::uint32_t> up =
                            detail::make_reduce_scratch<std::uint32_t>(
                                ctx_, up_scratch_count, counts.device_index(),
                                counts.residency());
                        up_scratch = up.get();
                        reduce_scratch_.push_back(std::static_pointer_cast<void>(up));
                    }

                    std::uint32_t* down_scratch = nullptr;
                    if (down_scratch_count != 0)
                    {
                        std::shared_ptr<std::uint32_t> down =
                            detail::make_reduce_scratch<std::uint32_t>(
                                ctx_, down_scratch_count, counts.device_index(),
                                counts.residency());
                        down_scratch = down.get();
                        reduce_scratch_.push_back(std::static_pointer_cast<void>(down));
                    }

                    // state[i] is level i's live in_count; state[level_count+i] its out_count.
                    const std::size_t state_words = 2 * level_count;
                    std::shared_ptr<std::uint32_t> state =
                        detail::make_reduce_scratch<std::uint32_t>(
                            ctx_, state_words, counts.device_index(), counts.residency());
                    reduce_scratch_.push_back(std::static_pointer_cast<void>(state));
                    std::uint32_t* state_ptr = state.get();
                    const Core::ResourceRegion state_region{
                        state_ptr, 0, state_words * sizeof(std::uint32_t)};

                    // One token is shared by every node this call emits.
                    auto registration_token = std::make_shared<std::optional<std::uint32_t>>();
                    reduce_registration_tokens_.push_back(registration_token);

                    emit_late_count_resolver(std::array<LateCount, 1>{n}, counts.device_index(),
                        Writes(state_region),
                        [state_ptr, level_count](const std::array<std::size_t, 1>& live)
                        {
                            detail::fill_live_level_words(level_count, live[0], state_ptr);
                        },
                        registration_token, std::size_t{1});

                    void* up_key = static_cast<void*>(up_scratch);
                    void* down_key = static_cast<void*>(down_scratch);
                    constexpr std::size_t tile = detail::REDUCE_TILE;

                    // The up-sweep folds every level but the top into scratch for the down-sweep.
                    for (std::size_t i = 0; i + 1 < level_count; ++i)
                    {
                        const detail::ReduceLevel& level = levels[i];
                        const bool first = (i == 0);

                        const std::uint32_t* src =
                            first ? counts_base : (up_scratch + level.in_offset);
                        std::uint32_t* dst = up_scratch + level.out_offset;

                        Reads reads = first
                            ? Reads(counts, state_region)
                            : Reads(Core::ResourceRegion{up_key,
                                                         level.in_offset * sizeof(std::uint32_t),
                                                         level.in_count * sizeof(std::uint32_t)},
                                    state_region);
                        Writes writes = Writes(Core::ResourceRegion{
                            up_key, level.out_offset * sizeof(std::uint32_t),
                            level.out_count * sizeof(std::uint32_t)});

                        observe(reads);
                        observe(writes);
                        emit_device_sized(reads.keys, writes.keys, level.out_count,
                            state_ptr + level_count + i, state_ptr + i,
                            [src, dst](std::size_t t, std::size_t in_count)
                            {
                                const std::size_t begin = t * tile;
                                std::size_t end = begin + tile;
                                if (end > in_count) end = in_count;
                                dst[t] = detail::fold_tile(
                                    [](std::uint32_t a, std::uint32_t b) { return a + b; },
                                    src, begin, end);
                            },
                            registration_token, level.out_count);
                    }

                    // The top level scans from a base of zero and always runs, to publish total.
                    for (std::size_t i = level_count; i-- > 0;)
                    {
                        const detail::ReduceLevel& level = levels[i];
                        const bool first = (i == 0);
                        const bool top = (i + 1 == level_count);

                        const std::uint32_t* src =
                            first ? counts_base : (up_scratch + level.in_offset);
                        std::uint32_t* dst =
                            first ? offsets_base : (down_scratch + level.in_offset);

                        if (top)
                        {
                            Reads reads = first
                                ? Reads(counts, state_region)
                                : Reads(Core::ResourceRegion{
                                      up_key, level.in_offset * sizeof(std::uint32_t),
                                      level.in_count * sizeof(std::uint32_t)},
                                        state_region);
                            Writes writes = first
                                ? Writes(offsets, total)
                                : Writes(Core::ResourceRegion{
                                      down_key, level.in_offset * sizeof(std::uint32_t),
                                      level.in_count * sizeof(std::uint32_t)},
                                        total);
                            observe(reads);
                            observe(writes);
                            emit_device_sized(reads.keys, writes.keys, std::size_t{1},
                                nullptr, state_ptr + i,
                                [src, dst, offsets_base, total_base, first](
                                    std::size_t, std::size_t in_count)
                                {
                                    std::uint32_t acc = 0;
                                    for (std::size_t j = 0; j < in_count; ++j)
                                    {
                                        dst[j] = acc;
                                        acc += src[j];
                                    }
                                    total_base[0] = acc;
                                    if (first)
                                        offsets_base[in_count] = acc;
                                },
                                registration_token, std::size_t{1});
                            continue;
                        }

                        // Each tile's base is the exclusive prefix the level above produced for it.
                        const std::uint32_t* base_arr = down_scratch + level.out_offset;

                        Reads reads = first
                            ? Reads(counts, Core::ResourceRegion{
                                  down_key, level.out_offset * sizeof(std::uint32_t),
                                  level.out_count * sizeof(std::uint32_t)}, state_region)
                            : Reads(Core::ResourceRegion{
                                  up_key, level.in_offset * sizeof(std::uint32_t),
                                  level.in_count * sizeof(std::uint32_t)},
                                    Core::ResourceRegion{
                                        down_key, level.out_offset * sizeof(std::uint32_t),
                                        level.out_count * sizeof(std::uint32_t)},
                                    state_region);
                        Writes writes = first
                            ? Writes(offsets)
                            : Writes(Core::ResourceRegion{
                                  down_key, level.in_offset * sizeof(std::uint32_t),
                                  level.in_count * sizeof(std::uint32_t)});

                        observe(reads);
                        observe(writes);
                        emit_device_sized(reads.keys, writes.keys, level.out_count,
                            state_ptr + level_count + i, state_ptr + i,
                            [src, dst, base_arr](std::size_t t, std::size_t in_count)
                            {
                                const std::size_t begin = t * tile;
                                std::size_t end = begin + tile;
                                if (end > in_count) end = in_count;
                                std::uint32_t acc = base_arr[t];
                                for (std::size_t j = begin; j < end; ++j)
                                {
                                    dst[j] = acc;
                                    acc += src[j];
                                }
                            },
                            registration_token, level.out_count);
                    }

                    if (level_count == 1)
                        return *this;

                    // The closing write copies the settled total into offsets[live n].
                    Reads closing_reads = Reads(total, state_region);
                    Writes closing_writes = Writes(offsets);
                    observe(closing_reads);
                    observe(closing_writes);
                    emit_device_sized(closing_reads.keys, closing_writes.keys, std::size_t{1},
                        nullptr, state_ptr,
                        [offsets_base, total_base](std::size_t, std::size_t live_n)
                        {
                            offsets_base[live_n] = total_base[0];
                        },
                        registration_token, std::size_t{1});
                    return *this;
                }

                /**
                 * @brief Stably argsorts keys by 8-bit digits into bucket offsets.
                 * @throws Core::SushiException If a buffer is too small, if @p keys,
                 *        @p indices, @p offsets or @p counts sit on different
                 *        devices, if @p key_range.capacity is zero, or at run if
                 *        either live count exceeds its capacity.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                Graph& add_counting_sort(const Buffer<std::uint32_t>& keys,
                                         const Buffer<std::uint32_t>& indices,
                                         const Buffer<std::uint32_t>& offsets,
                                         const Buffer<std::uint32_t>& total,
                                         LateCount n, LateCount key_range,
                                         const Buffer<std::uint32_t>* counts = nullptr)
                {
                    detail::validate_late_count(n, "add_counting_sort");
                    detail::validate_late_count(key_range, "add_counting_sort");
                    if (key_range.capacity == 0)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_counting_sort: key_range.capacity is zero."});
                    if (n.capacity > keys.size())
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_counting_sort: n.capacity (" + std::to_string(n.capacity) +
                            ") exceeds the keys buffer's size (" +
                            std::to_string(keys.size()) + ")."});
                    if (indices.size() < n.capacity)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_counting_sort: indices holds " +
                            std::to_string(indices.size()) + " elements but " +
                            std::to_string(n.capacity) + " are required."});
                    if (offsets.size() < key_range.capacity + 1)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_counting_sort: offsets holds " +
                            std::to_string(offsets.size()) + " elements but " +
                            std::to_string(key_range.capacity + 1) + " are required."});
                    if (total.size() == 0)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_counting_sort: the total buffer holds no elements."});
                    if (counts != nullptr && counts->size() < key_range.capacity)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_counting_sort: counts holds " +
                            std::to_string(counts->size()) + " elements but " +
                            std::to_string(key_range.capacity) + " are required."});
                    if (indices.device_index() != keys.device_index() ||
                        offsets.device_index() != keys.device_index() ||
                        (counts != nullptr && counts->device_index() != keys.device_index()))
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "add_counting_sort: keys, indices, offsets and counts must "
                            "live on the same device."});

                    constexpr std::size_t DIGIT_BINS = detail::RADIX_DIGIT_BINS;
                    const std::size_t digit_count = detail::radix_digit_count(key_range.capacity);

                    // One token shared by every node this call emits (G1 spec §F5).
                    auto registration_token = std::make_shared<std::optional<std::uint32_t>>();
                    reduce_registration_tokens_.push_back(registration_token);

                    // n_state[0] is the live element count, n_state[1] its live tile count.
                    std::shared_ptr<std::uint32_t> n_state = detail::make_reduce_scratch<std::uint32_t>(
                        ctx_, 2, keys.device_index(), keys.residency());
                    reduce_scratch_.push_back(std::static_pointer_cast<void>(n_state));
                    std::uint32_t* n_state_ptr = n_state.get();
                    const Core::ResourceRegion n_state_region{n_state_ptr, 0, 2 * sizeof(std::uint32_t)};

                    // Per digit, the plan holds live work counts and where it places.
                    std::uint32_t* plan_ptr = nullptr;
                    if (n.capacity != 0)
                    {
                        std::shared_ptr<std::uint32_t> plan = detail::make_reduce_scratch<std::uint32_t>(
                            ctx_, digit_count * detail::RADIX_PLAN_WORDS, keys.device_index(),
                            keys.residency());
                        plan_ptr = plan.get();
                        reduce_scratch_.push_back(std::static_pointer_cast<void>(plan));
                    }
                    const Core::ResourceRegion plan_region{plan_ptr, 0,
                        digit_count * detail::RADIX_PLAN_WORDS * sizeof(std::uint32_t)};

                    // One kernel resolves both counts, publishes total and fills the digit plan (design doc §A3).
                    std::uint32_t* total_base = total.data();
                    emit_late_count_resolver(std::array<LateCount, 2>{n, key_range},
                        keys.device_index(),
                        (plan_ptr != nullptr) ? Writes(n_state_region, total, plan_region)
                                              : Writes(n_state_region, total),
                        [n_state_ptr, total_base, plan_ptr, digit_count](
                            const std::array<std::size_t, 2>& live)
                        {
                            const std::size_t tiles = detail::next_live_tile_count(live[0]);
                            n_state_ptr[0] = static_cast<std::uint32_t>(live[0]);
                            n_state_ptr[1] = static_cast<std::uint32_t>(tiles);
                            total_base[0] = static_cast<std::uint32_t>(live[0]);
                            if (plan_ptr != nullptr)
                                detail::fill_radix_plan(digit_count, live[0], tiles, live[1], plan_ptr);
                        },
                        registration_token, std::size_t{1});

                    if (n.capacity == 0)
                    {
                        // No element ever arrives, so every bucket starts and stays at zero.
                        std::uint32_t* offsets_base = offsets.data();
                        std::uint32_t* counts_base = (counts != nullptr) ? counts->data() : nullptr;
                        Reads reads = Reads();
                        Writes writes = (counts != nullptr) ? Writes(offsets, *counts) : Writes(offsets);
                        observe(reads);
                        observe(writes);
                        emit_device_sized(reads.keys, writes.keys, key_range.capacity + 1,
                            nullptr, nullptr,
                            [offsets_base, counts_base, capacity = key_range.capacity](std::size_t k)
                            {
                                offsets_base[k] = 0;
                                if (counts_base != nullptr && k < capacity)
                                    counts_base[k] = 0;
                            },
                            registration_token, key_range.capacity + 1);
                        return *this;
                    }

                    const std::size_t T_cap = detail::next_live_tile_count(n.capacity);
                    constexpr std::size_t tile = detail::REDUCE_TILE;

                    // hist holds raw counts until the column scan overwrites it in place with prefixes.
                    std::shared_ptr<std::uint32_t> hist = detail::make_reduce_scratch<std::uint32_t>(
                        ctx_, DIGIT_BINS * T_cap, keys.device_index(), keys.residency());
                    reduce_scratch_.push_back(std::static_pointer_cast<void>(hist));
                    std::shared_ptr<std::uint32_t> col_total = detail::make_reduce_scratch<std::uint32_t>(
                        ctx_, DIGIT_BINS, keys.device_index(), keys.residency());
                    reduce_scratch_.push_back(std::static_pointer_cast<void>(col_total));
                    std::shared_ptr<std::uint32_t> bin_base = detail::make_reduce_scratch<std::uint32_t>(
                        ctx_, DIGIT_BINS, keys.device_index(), keys.residency());
                    reduce_scratch_.push_back(std::static_pointer_cast<void>(bin_base));

                    // One scratch pings against the indices; a single digit never needs it.
                    std::uint32_t* scratch_base = nullptr;
                    if (digit_count >= 2)
                    {
                        std::shared_ptr<std::uint32_t> owned = detail::make_reduce_scratch<std::uint32_t>(
                            ctx_, n.capacity, keys.device_index(), keys.residency());
                        scratch_base = owned.get();
                        reduce_scratch_.push_back(std::static_pointer_cast<void>(owned));
                    }

                    const std::uint32_t* keys_base = keys.data();
                    std::uint32_t* indices_base = indices.data();
                    std::uint32_t* hist_base = hist.get();
                    std::uint32_t* col_total_base = col_total.get();
                    std::uint32_t* bin_base_base = bin_base.get();

                    const Core::ResourceRegion indices_region{static_cast<void*>(indices_base), 0,
                        n.capacity * sizeof(std::uint32_t)};
                    const Core::ResourceRegion scratch_region{static_cast<void*>(scratch_base), 0,
                        n.capacity * sizeof(std::uint32_t)};
                    const Core::ResourceRegion hist_region{static_cast<void*>(hist_base), 0,
                        DIGIT_BINS * T_cap * sizeof(std::uint32_t)};
                    const Core::ResourceRegion col_total_region{static_cast<void*>(col_total_base), 0,
                        DIGIT_BINS * sizeof(std::uint32_t)};
                    const Core::ResourceRegion bin_base_region{static_cast<void*>(bin_base_base), 0,
                        DIGIT_BINS * sizeof(std::uint32_t)};

                    // Index of one word of one digit's plan.
                    const auto plan_word = [](std::size_t digit, std::size_t word) {
                        return digit * detail::RADIX_PLAN_WORDS + word;
                    };

                    for (std::size_t d = 0; d < digit_count; ++d)
                    {
                        const bool first = (d == 0);
                        const std::uint32_t shift = static_cast<std::uint32_t>(8 * d);
                        std::uint32_t* digit_plan = plan_ptr + d * detail::RADIX_PLAN_WORDS;
                        const std::uint32_t* snapshot = digit_plan + detail::RADIX_PLAN_LIVE_N;

                        // A later digit reads whichever buffer the previous digit filled.
                        Reads sources = first ? Reads()
                                              : (scratch_base != nullptr
                                                    ? Reads(indices_region, scratch_region)
                                                    : Reads(indices_region));

                        // Histogram: each tile overwrites its own row, so no run clears it.
                        {
                            Reads reads = Reads(keys, plan_region);
                            for (const Core::ResourceRegion& r : sources.keys)
                                reads.keys.push_back(r);
                            Writes writes = Writes(hist_region);
                            observe(reads);
                            observe(writes);
                            emit_device_sized(reads.keys, writes.keys, T_cap,
                                plan_ptr + plan_word(d, detail::RADIX_PLAN_TILES), snapshot,
                                [keys_base, indices_base, scratch_base, hist_base, first, shift,
                                 T_cap](std::size_t t, std::size_t live_n, std::size_t into_indices)
                                {
                                    const std::uint32_t* src_indices =
                                        into_indices != 0 ? scratch_base : indices_base;
                                    std::uint32_t local[DIGIT_BINS] = {};
                                    const std::size_t begin = t * tile;
                                    std::size_t end = begin + tile;
                                    if (end > live_n) end = live_n;
                                    for (std::size_t i = begin; i < end; ++i)
                                    {
                                        const std::size_t src = first ? i : src_indices[i];
                                        const std::uint32_t digit =
                                            (keys_base[src] >> shift) & 0xFFu;
                                        ++local[digit];
                                    }
                                    for (std::size_t b = 0; b < DIGIT_BINS; ++b)
                                        hist_base[b * T_cap + t] = local[b];
                                },
                                registration_token, T_cap);
                        }

                        // Column scan: one work-item per bin scans its tiles left to right.
                        {
                            // In place over hist: each bin's column is touched by exactly one work-item, sequentially.
                            Reads reads = Reads(hist_region, n_state_region, plan_region);
                            Writes writes = Writes(hist_region, col_total_region);
                            observe(reads);
                            observe(writes);
                            emit_device_sized(reads.keys, writes.keys, DIGIT_BINS,
                                plan_ptr + plan_word(d, detail::RADIX_PLAN_BINS), n_state_ptr + 1,
                                [hist_base, col_total_base, T_cap](
                                    std::size_t b, std::size_t live_T)
                                {
                                    std::uint32_t running = 0;
                                    for (std::size_t t = 0; t < live_T; ++t)
                                    {
                                        const std::uint32_t count = hist_base[b * T_cap + t];
                                        hist_base[b * T_cap + t] = running;
                                        running += count;
                                    }
                                    col_total_base[b] = running;
                                },
                                registration_token, DIGIT_BINS);
                        }

                        // Bin base: one work-item walks the 256 bin totals, whatever n is.
                        {
                            Reads reads = Reads(col_total_region, plan_region);
                            Writes writes = Writes(bin_base_region);
                            observe(reads);
                            observe(writes);
                            emit_device_sized(reads.keys, writes.keys, std::size_t{1},
                                plan_ptr + plan_word(d, detail::RADIX_PLAN_ONE), nullptr,
                                [col_total_base, bin_base_base](std::size_t)
                                {
                                    std::uint32_t running = 0;
                                    for (std::size_t b = 0; b < DIGIT_BINS; ++b)
                                    {
                                        bin_base_base[b] = running;
                                        running += col_total_base[b];
                                    }
                                },
                                registration_token, std::size_t{1});
                        }

                        // Placement: tiles write in ascending index order, so it is stable.
                        {
                            Reads reads = Reads(keys, hist_region, bin_base_region, plan_region);
                            for (const Core::ResourceRegion& r : sources.keys)
                                reads.keys.push_back(r);
                            Writes writes = (scratch_base != nullptr)
                                ? Writes(indices_region, scratch_region)
                                : Writes(indices_region);
                            observe(reads);
                            observe(writes);
                            emit_device_sized(reads.keys, writes.keys, T_cap,
                                plan_ptr + plan_word(d, detail::RADIX_PLAN_TILES), snapshot,
                                [keys_base, indices_base, scratch_base, hist_base, bin_base_base,
                                 first, shift, T_cap](
                                    std::size_t t, std::size_t live_n, std::size_t into_indices)
                                {
                                    const std::uint32_t* src_indices =
                                        into_indices != 0 ? scratch_base : indices_base;
                                    std::uint32_t* dst_indices =
                                        into_indices != 0 ? indices_base : scratch_base;
                                    std::uint32_t cursor[DIGIT_BINS];
                                    for (std::size_t b = 0; b < DIGIT_BINS; ++b)
                                        cursor[b] = bin_base_base[b] + hist_base[b * T_cap + t];
                                    const std::size_t begin = t * tile;
                                    std::size_t end = begin + tile;
                                    if (end > live_n) end = live_n;
                                    for (std::size_t i = begin; i < end; ++i)
                                    {
                                        const std::size_t src = first ? i : src_indices[i];
                                        const std::uint32_t digit =
                                            (keys_base[src] >> shift) & 0xFFu;
                                        dst_indices[cursor[digit]++] =
                                            static_cast<std::uint32_t>(src);
                                    }
                                },
                                registration_token, T_cap);
                        }
                    }

                    // Offsets: one lower_bound per bucket; no live bucket count is read.
                    {
                        std::uint32_t* offsets_base = offsets.data();
                        Reads reads = Reads(keys, indices, n_state_region);
                        Writes writes = Writes(offsets);
                        observe(reads);
                        observe(writes);
                        emit_device_sized(reads.keys, writes.keys, key_range.capacity + 1,
                            nullptr, n_state_ptr,
                            [keys_base, indices_base, offsets_base](
                                std::size_t k, std::size_t live_n)
                            {
                                std::size_t lo = 0;
                                std::size_t hi = live_n;
                                while (lo < hi)
                                {
                                    const std::size_t mid = lo + (hi - lo) / 2;
                                    if (keys_base[indices_base[mid]] < k)
                                        lo = mid + 1;
                                    else
                                        hi = mid;
                                }
                                offsets_base[k] = static_cast<std::uint32_t>(lo);
                            },
                            registration_token, key_range.capacity + 1);
                    }

                    if (counts == nullptr)
                        return *this;

                    // Counts are differences of two settled offsets and need no live scalar.
                    std::uint32_t* offsets_base = offsets.data();
                    std::uint32_t* counts_base = counts->data();
                    Reads reads = Reads(offsets);
                    Writes writes = Writes(*counts);
                    observe(reads);
                    observe(writes);
                    emit_device_sized(reads.keys, writes.keys, key_range.capacity,
                        nullptr, nullptr,
                        [offsets_base, counts_base](std::size_t k)
                        {
                            counts_base[k] = offsets_base[k + 1] - offsets_base[k];
                        },
                        registration_token, key_range.capacity);
                    return *this;
                }

                /**
                 * @brief Adds an offloadable task: a named remote kernel with a local fallback.
                 * @param op OpID of the kernel, e.g. "vec.add"_op.
                 * @param local Local fallback invoked as local(i) for i in [0, n).
                 * @see include/SushiRuntime/api/graph/README.md
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
                 * @brief Adds an offloadable task that names its data with Reads and Writes.
                 * @param local Local fallback invoked as local(i) for i in [0, n).
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
                 * @pre s was registered with State::distributed().
                 * @param s The state this rule evolves; must outlive the graph.
                 * @param local Local fallback invoked as local(i, const T* cur, T* next).
                 * @see include/SushiRuntime/api/graph/README.md
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
                                // Read at each submit so the fallback follows the swap.
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

                    // Live IO is re-pointed before each step for the coordinator.
                    steps_.add_dist_io_updater([view, live_reads, live_writes]()
                    {
                        (*live_reads)[0]  = view->cur;
                        (*live_writes)[0] = view->next;
                    });

                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Reports how much work has been recorded.
                 * @return Number of update rules (tasks) added so far.
                 */
                std::size_t size() const noexcept { return task_count_; }

                /** @brief Stands for "this rule has no index yet". */
                static constexpr std::uint32_t no_registration = BackingGraph::NO_REGISTRATION;

                /**
                 * @brief Returns the registration index the rule at position rule took.
                 * @param rule Position in add() order, below size().
                 * @return The index, or no_registration when rule is out of range or nothing
                 *        compiled yet.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                std::uint32_t rule_registration(std::size_t rule) const noexcept
                {
                    return rule < specs_.size() ? specs_[rule].registration : no_registration;
                }

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
                 * @brief Requests or withdraws per-node device timing and marks the plan dirty.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                void set_profiling_requested(bool requested) noexcept
                {
                    profiling_requested_ = requested;
                    dirty_ = true;
                }

                /**
                 * @brief Reports whether this graph currently requests profiling.
                 * @return The last value set by set_profiling_requested(), or the
                 *         runtime's RuntimeSettings::profiling default.
                 */
                bool profiling_requested() const noexcept { return profiling_requested_; }

                /**
                 * @brief Runs the graph once, blocking until it completes.
                 * @return A RunReport summarising the run.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport run() { return run(std::size_t{1}); }

                /**
                 * @brief Runs the given number of successive steps and blocks until they finish.
                 * @return A RunReport aggregating the whole run.
                 * @throws Core::SushiException on any synchronous or device error.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                Core::RunReport run(std::size_t steps)
                {
                    return unwrap(try_run(steps));
                }

                /**
                 * @brief Runs the graph once into a report the caller owns.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @throws Core::SushiException on any synchronous or device error.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                Core::RunReport& run(Core::RunReport& reuse)
                {
                    return run(std::size_t{1}, reuse);
                }

                /**
                 * @brief Runs @p steps successive steps into a report the caller owns.
                 *
                 * @param steps Number of steps to run.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A reference to @p reuse.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport& run(std::size_t steps, Core::RunReport& reuse)
                {
                    auto driven = try_run(steps, reuse);
                    if (!driven)
                        throw Core::SushiException(std::move(driven.error()));
                    return reuse;
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
                 * @brief Starts one step on the worker pool and returns without waiting for it.
                 * @pre The returned handle does not outlive this graph.
                 * @throws Core::SushiException If the graph has no tasks, references
                 *        a freed Buffer/State, the engine does not support
                 *        asynchronous runs, or a run is already in progress.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                RunHandle<Graph> run_async()
                {
                    if (task_count_ == 0)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                                                                "Graph has no tasks to run."});
                    if (async_step_pending_)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "A step started with run_async() is still in flight; wait "
                            "on its handle before starting another."});
                    if (has_dangling_resource())
                        throw Core::SushiException(Core::Error{Core::Errc::dangling_resource,
                            "Graph references a Buffer/State freed before the run."});

                    auto& engine = ctx_->get_execution_engine();
                    if (!engine.supports_asynchronous_run())
                        throw Core::SushiException(Core::Error{Core::Errc::unsupported,
                            "The active execution engine does not support run_async()."});

                    using clock = std::chrono::steady_clock;
                    detail::AsyncRunState state;

                    if (dirty_)
                    {
                        const auto compile_started = clock::now();
                        build_plan();
                        state.compile_ms = std::chrono::duration<double, std::milli>(
                            clock::now() - compile_started).count();
                    }

                    // The pre-step order here matches the synchronous driver's.
                    steps_.resync();
                    engine.reset_cancellation();
                    if (plan_.profiling)
                        engine.reset_profiling(plan_);

                    state.before = engine.metrics();
                    const Observability::TransferCounters& transfers = ctx_->transfers();
                    state.device_to_host_before = transfers.device_to_host();
                    state.host_to_device_before = transfers.host_to_device();
                    state.task_count = task_count_;

                    steps_.before_step();
                    reset_plan_state();
                    state.started = clock::now();
                    engine.submit(plan_);

                    async_step_pending_ = true;
                    return RunHandle<Graph>(this, state);
                }

                /**
                 * @brief Requests cancellation of an in-progress run, from any thread, without
                 *        throwing.
                 * @see include/SushiRuntime/api/graph/README.md
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
                    return drive([steps](std::size_t k) { return k < steps; }, steps,
                                 StepSync::AtEnd);
                }

                /**
                 * @brief Non-throwing single run into a report the caller owns.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A pointer to @p reuse on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport*, Core::Error>
                try_run(Core::RunReport& reuse) noexcept
                {
                    return try_run(std::size_t{1}, reuse);
                }

                /**
                 * @brief Non-throwing multi-step run into a report the caller owns.
                 * @param steps Number of steps to run.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A pointer to @p reuse on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport*, Core::Error>
                try_run(std::size_t steps, Core::RunReport& reuse) noexcept
                {
                    return drive([steps](std::size_t k) { return k < steps; }, reuse, steps,
                                 StepSync::AtEnd);
                }

            private:
                /** @brief Says whether a chained run may skip the host wait between steps. */
                enum class StepSync
                {
                    AtEnd,     /**< Fixed step count: submit back to back, one wait at the end. */
                    EveryStep  /**< The stop test may read results: wait after every step. */
                };

                friend class Runtime;
                // Region derives from Graph, and DynamicGraph composes its recorded specs.
                friend class Region;
                friend class DynamicGraph;
                // RunHandle settles a step through the two private members below and nothing else.
                template <class Owner> friend class RunHandle;
                friend struct detail::RegistrationIndexTestAccess;
                friend struct detail::NodeEventRecordsTestAccess;

                /** @brief The engine a pending asynchronous step is running on. */
                Execution::IExecutionEngine& async_engine()
                {
                    return ctx_->get_execution_engine();
                }

                /**
                 * @brief Releases the in-flight marker for a step that ended badly.
                 *
                 * A task exception leaves the step over but unsettled: no report, no
                 * State advance. The graph must still become runnable again, or one
                 * throwing kernel would permanently refuse every later run.
                 */
                void discard_async_step() noexcept { async_step_pending_ = false; }

                /**
                 * @brief Closes out an asynchronous step once the engine reports it done.
                 * @param report The report to fill; reset before it is written.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                void finish_async_step(const detail::AsyncRunState& state,
                                       Core::RunReport& report)
                {
                    const auto finished = std::chrono::steady_clock::now();
                    auto& engine = ctx_->get_execution_engine();

                    async_step_pending_ = false;
                    steps_.check_faults();
                    steps_.after_step();

                    const auto after = engine.metrics();
                    detail::fill_run_report(
                        report, *ctx_, state.before, after, state.task_count, state.compile_ms,
                        std::chrono::duration<double, std::milli>(
                            finished - state.started).count());

                    report.cancelled = engine.is_cancelled();
                    const Observability::TransferCounters& transfers = ctx_->transfers();
                    report.device_to_host_bytes =
                        transfers.device_to_host() - state.device_to_host_before;
                    report.host_to_device_bytes =
                        transfers.host_to_device() - state.host_to_device_before;

                    if (plan_.profiling)
                        engine.collect_profiling(plan_, report);
                }

                explicit Graph(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx)), profiling_requested_(ctx_->settings().profiling)
                {
                }

                /** @brief Throws the carried error, or returns the report. */
                static Core::RunReport unwrap(Core::Result<Core::RunReport, Core::Error> r)
                {
                    if (!r)
                        throw Core::SushiException(std::move(r.error()));
                    return std::move(*r);
                }

                /**
                 * @brief Non-throwing run driver shared by every run/try_run entry.
                 *
                 * @param keep_going Called with the count of completed steps before
                 *                   each step; the loop continues while it is true.
                 * @return The aggregated RunReport, or a mapped Error on failure.
                 */
                template <class KeepGoing>
                Core::Result<Core::RunReport, Core::Error>
                drive(KeepGoing keep_going, std::size_t expected_invocations = 1,
                      StepSync sync = StepSync::EveryStep) noexcept
                {
                    Core::RunReport report;
                    auto driven = drive(keep_going, report, expected_invocations, sync);
                    if (!driven)
                        return Core::make_error(std::move(driven.error()));
                    return std::move(report);
                }

                /**
                 * @brief Drives the run loop into a caller-owned report; every run entry goes
                 *        through here.
                 * @param keep_going Called with the completed step count before each step; true
                 *        continues.
                 * @param report The report to fill; reset before the run starts.
                 * @return A pointer to report on success, or a mapped Error.
                 */
                template <class KeepGoing>
                Core::Result<Core::RunReport*, Core::Error>
                drive(KeepGoing keep_going, Core::RunReport& report,
                     std::size_t expected_invocations = 1,
                     StepSync sync = StepSync::EveryStep) noexcept
                {
                    // Reset before any early return, so a failed run shows no old numbers.
                    report.reset();
                    report.is_successful = false;

                    if (task_count_ == 0)
                        return Core::make_error(Core::Error{Core::Errc::invalid_graph,
                                                            "Graph has no tasks to run."});
                    // A synchronous run must not re-arm nodes during an asynchronous step.
                    if (async_step_pending_)
                        return Core::make_error(Core::Error{Core::Errc::invalid_graph,
                            "A step started with run_async() is still in flight; wait "
                            "on its handle before running this graph again."});
                    if (has_dangling_resource())
                        return Core::make_error(Core::Error{Core::Errc::dangling_resource,
                            "Graph references a Buffer/State freed before the run."});
                    try
                    {
                        drive_impl(keep_going, report, expected_invocations, sync);
                        return &report;
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
                void drive_impl(KeepGoing keep_going, Core::RunReport& report,
                                std::size_t expected_invocations = 1,
                                StepSync sync = StepSync::EveryStep)
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

                    // State views point at their current and next buffers before the first step.
                    steps_.resync();

                    auto& engine = ctx_->get_execution_engine();

                    // A cancel that ended an earlier run must not abort this one.
                    engine.reset_cancellation();

                    // Profiling accumulators are cleared only when the plan requests profiling.
                    const bool profiling = plan_.profiling;
                    if (profiling)
                        engine.reset_profiling(plan_, expected_invocations);

                    const auto before = engine.metrics();
                    const Observability::TransferCounters& transfers = ctx_->transfers();
                    const std::size_t d2h_before = transfers.device_to_host();
                    const std::size_t h2d_before = transfers.host_to_device();
                    const auto t0 = clock::now();

                    const bool chained = plan_.chain.chainable && !ctx_->settings().force_scheduler;
                    const std::size_t executed = chained
                        ? run_chained(keep_going, engine, sync)
                        : run_scheduled(keep_going, engine);

                    const auto t1 = clock::now();
                    const auto after = engine.metrics();

                    const std::size_t total_tasks = task_count_ * executed;
                    detail::fill_run_report(report, *ctx_, before, after, total_tasks,
                                            compile_ms, ms(t0, t1));
                    report.cancelled = engine.is_cancelled();
                    report.layer = chained ? Core::ExecutionLayer::Chain
                                           : Core::ExecutionLayer::Scheduler;

                    // Only explicit transfers are counted; implicit USM migration is not.
                    report.device_to_host_bytes = transfers.device_to_host() - d2h_before;
                    report.host_to_device_bytes = transfers.host_to_device() - h2d_before;

                    // Per-node and per-worker timing, only when profiling is on.
                    if (profiling)
                        engine.collect_profiling(plan_, report);
                }

                /**
                 * @brief Runs the steps on the work-stealing scheduler, waiting after each.
                 * @param keep_going Called with the count of completed steps before each step.
                 * @param engine     The context's execution engine.
                 * @return The number of steps run.
                 */
                template <class KeepGoing>
                std::size_t run_scheduled(KeepGoing& keep_going, Execution::IExecutionEngine& engine)
                {
                    std::size_t executed = 0;
                    while (keep_going(executed) && !engine.is_cancelled())
                    {
                        // Live IO and late-bound controls are refreshed before the plan replays.
                        steps_.before_step();

                        reset_plan_state();
                        engine.execute(plan_); // lvalue -> non-consuming replay
                        steps_.check_faults();

                        // States advance after the step so the written next field becomes current.
                        steps_.after_step();
                        ++executed;
                    }
                    return executed;
                }

                /**
                 * @brief Runs the steps as an in-order chain from this thread.
                 * @param sync AtEnd for a fixed step count, EveryStep when the stop test may read
                 *        results.
                 * @return The number of steps run.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <class KeepGoing>
                std::size_t run_chained(KeepGoing& keep_going, Execution::IExecutionEngine& engine,
                                        StepSync sync)
                {
                    Execution::ChainRun chain(*ctx_, plan_);
                    std::size_t executed = 0;
                    while (keep_going(executed) && !engine.is_cancelled())
                    {
                        steps_.before_step();
                        reset_plan_state();
                        chain.submit_step(static_cast<std::uint32_t>(executed));
                        if (sync == StepSync::EveryStep)
                        {
                            chain.wait();
                            steps_.check_faults();
                        }
                        steps_.after_step();
                        ++executed;
                    }
                    chain.finish();
                    steps_.check_faults();
                    return executed;
                }

                /**
                 * @brief Appends a capture wrapper's key to the read list, the write list, or both
                 *        for InOut.
                 */
                template <class Cap>
                static void collect_keys(std::vector<Core::ResourceRegion>& reads,
                                         std::vector<Core::ResourceRegion>& writes, const Cap& c)
                {
                    if (Cap::reads)
                        reads.push_back(c.key);
                    if (Cap::writes)
                        writes.push_back(c.key);
                }

                /**
                 * @brief Records the liveness observers of the typed handles a task touches.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
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

                /** @brief Returns whether any recorded handle's allocation has been freed. */
                bool has_dangling_resource() const
                {
                    for (const auto& o : observers_)
                        if (o.expired())
                            return true;
                    return false;
                }

                /**
                 * @brief Emits the coupled-State node once the callable is split from the read
                 *        States.
                 */
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
                            // Resolved host-side at submit, so the kernel captures raw pointers.
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

                /**
                 * @brief Emits the capture-overload node once the callable is split from the
                 *        captures.
                 */
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

                /**
                 * @brief Emits a 1-D per-element tracked task for both tracking overloads.
                 * @param registration_token Shared slot grouping this node's index with the others
                 *        of one
                 *        add() call; null draws a fresh index.
                 * @param width This node's own launch width.
                 */
                template <class F>
                Graph& emit_per_element_tracked(std::vector<Core::ResourceRegion> reads,
                                                std::vector<Core::ResourceRegion> writes,
                                                std::size_t n, F&& fn,
                                                std::shared_ptr<std::optional<std::uint32_t>>
                                                    registration_token = nullptr,
                                                std::size_t width = Core::WIDTH_UNKNOWN)
                {
                    specs_.push_back([fn = std::forward<F>(fn), n,
                                      reads = std::move(reads),
                                      writes = std::move(writes),
                                      registration_token, width](BackingGraph& bg)
                    {
                        std::optional<std::uint32_t> registration;
                        if (registration_token)
                        {
                            if (!registration_token->has_value())
                                *registration_token = bg.reserve_registration();
                            registration = *registration_token;
                        }
                        bg.add_node([fn, n](sycl::handler& h)
                        {
                            h.parallel_for(sycl::range<1>(n),
                                           [fn](sycl::id<1> i) { fn(i[0]); });
                        }, reads, writes, {}, registration);
                        bg.width_last_node(width);
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Holds the half-open element window a late-bound node iterates this step.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                struct LiveRange
                {
                    std::size_t base = 0;
                    std::size_t count = 0;
                };

                /**
                 * @brief Resolves a host-sized late-bound node's base and count at dispatch.
                 *
                 * Reads the step-boundary values only, with USE_BAKED meaning the rest
                 * of capacity; a node with a device count or base never comes here.
                 * @throws Core::SushiException With invalid_graph when the window does not fit @p capacity.
                 */
                static LiveRange resolve_live_range(
                    const ::SushiRuntime::Graph::LateBindings& lb, std::size_t capacity)
                {
                    LiveRange range;
                    range.base = lb.base.load(std::memory_order_relaxed);

                    const std::size_t live = lb.count.load(std::memory_order_relaxed);
                    // USE_BAKED means the rest of the capacity from the base, not the whole buffer.
                    range.count =
                        (live == ::SushiRuntime::Graph::LateBindings::USE_BAKED)
                            ? capacity - (range.base < capacity ? range.base : capacity)
                            : live;

                    if (range.base > capacity || range.count > capacity - range.base)
                        throw Core::SushiException(Core::Error{
                            Core::Errc::invalid_graph,
                            "A late-bound task's live window starts at " +
                            std::to_string(range.base) + " and covers " +
                            std::to_string(range.count) +
                            " elements, which does not fit the capacity it was "
                            "compiled for (" + std::to_string(capacity) +
                            "); size the task's buffers for the worst case."});
                    return range;
                }

                /**
                 * @brief Submits a late-bound node's kernel over its live range, or an empty kernel
                 *        for none.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <class F>
                static void submit_live_range(sycl::handler& h, LiveRange range, const F& fn)
                {
                    if (range.count == 0)
                    {
                        h.single_task([]() {});
                        return;
                    }
                    // The base offset is applied here, so kernels always receive absolute indices.
                    const std::size_t base = range.base;
                    h.parallel_for(sycl::range<1>(range.count),
                                   [fn, base](sycl::id<1> i) { fn(base + i[0]); });
                }

                /**
                 * @brief Carries a device-sized node's window sources into its kernel.
                 */
                struct DeviceWindow
                {
                    const std::uint32_t* count_slot = nullptr; /**< Device-written count, or null. */
                    const std::uint32_t* base_slot = nullptr;  /**< Device-written base, or null. */
                    std::size_t host_count = 0;      /**< Host count when @c count_slot is null. */
                    std::size_t host_base = 0;       /**< Host base when @c base_slot is null. */
                    std::size_t capacity = 0;        /**< Capacity the window must fit. */
                    bool rest_of_capacity = false;   /**< Whether the host count is USE_BAKED. */
                    std::uint32_t* fault = nullptr;  /**< The node's fault record. */
                };

                /**
                 * @brief Submits a late-bound node, reading a device count or base inside the kernel.
                 *
                 * A node with no device slot keeps its host-resolved range. One with a
                 * slot runs at @p capacity, each work-item past the live count returns,
                 * and a window that does not fit is recorded in @p fault for the step to
                 * throw (docs/design/GRAPH_REPLAY.md §D).
                 */
                template <class F>
                static void submit_late_window(sycl::handler& h,
                                               const ::SushiRuntime::Graph::LateBindings& lb,
                                               std::size_t capacity, std::uint32_t* fault,
                                               const F& fn)
                {
                    if (lb.device_count == nullptr && lb.device_base == nullptr)
                    {
                        submit_live_range(h, resolve_live_range(lb, capacity), fn);
                        return;
                    }

                    DeviceWindow w;
                    w.count_slot = (lb.device_count != nullptr)
                        ? lb.device_count + lb.device_count_index : nullptr;
                    w.base_slot = (lb.device_base != nullptr)
                        ? lb.device_base + lb.device_base_index : nullptr;
                    w.host_count = lb.count.load(std::memory_order_relaxed);
                    w.host_base = lb.base.load(std::memory_order_relaxed);
                    w.capacity = capacity;
                    w.rest_of_capacity = (w.host_count == ::SushiRuntime::Graph::LateBindings::USE_BAKED);
                    w.fault = fault;

                    // One work-item even at capacity zero, so a fault there is still recorded.
                    h.parallel_for(sycl::range<1>(capacity == 0 ? 1 : capacity),
                                   [w, fn](sycl::id<1> i)
                    {
                        const std::size_t base = (w.base_slot != nullptr)
                            ? static_cast<std::size_t>(*w.base_slot) : w.host_base;
                        std::size_t count = w.host_count;
                        if (w.count_slot != nullptr)
                            count = static_cast<std::size_t>(*w.count_slot);
                        else if (w.rest_of_capacity)
                            count = w.capacity - (base < w.capacity ? base : w.capacity);
                        if (base > w.capacity || count > w.capacity - base)
                        {
                            if (i[0] == 0)
                                detail::record_device_fault(w.fault, detail::DEVICE_FAULT_WINDOW,
                                                            base, count, w.capacity);
                            return;
                        }
                        if (i[0] < count)
                            fn(base + i[0]);
                    });
                }

                /**
                 * @brief Allocates a zeroed fault record the step checks after it finishes.
                 * @param device Device whose context the record's host USM belongs to.
                 * @return DEVICE_FAULT_WORDS words this graph owns.
                 */
                std::uint32_t* allocate_fault_record(DeviceIndex device)
                {
                    std::shared_ptr<std::uint32_t> record = detail::make_reduce_scratch<std::uint32_t>(
                        ctx_, detail::DEVICE_FAULT_WORDS, device, Residency::Host);
                    for (std::size_t w = 0; w < detail::DEVICE_FAULT_WORDS; ++w)
                        record.get()[w] = 0;
                    reduce_scratch_.push_back(std::static_pointer_cast<void>(record));
                    steps_.register_fault_record(record.get());
                    return record.get();
                }

                /**
                 * @brief Allocates a fault record for a Dynamic with a device slot, on that slot's device.
                 * @param dyn A Dynamic that passed require_host_device_slots().
                 * @return The record, or null when @p dyn has no device slot.
                 */
                std::uint32_t* fault_record_for(const Dynamic& dyn)
                {
                    const std::uint32_t* slot = (dyn.device_count != nullptr) ? dyn.device_count
                                                                              : dyn.device_base;
                    if (slot == nullptr)
                        return nullptr;
                    void* address = const_cast<void*>(static_cast<const void*>(slot));
                    Memory::UsmAllocator& memory = ctx_->memory();
                    const std::optional<void*> base = memory.containing_base(address);
                    const std::optional<std::size_t> device =
                        memory.device_of(base ? *base : address);
                    return allocate_fault_record(DeviceIndex{device.value_or(0)});
                }

                /**
                 * @brief Emits the kernel that resolves a call's LateCounts for a run.
                 *
                 * A host count bakes in; a device count is read here, zeroed and faulted
                 * past capacity (docs/design/GRAPH_REPLAY.md §D.2).
                 * @param fill Device callable run once as fill(live), in @p counts' order.
                 * @param width This node's own launch width; stamped where it is added.
                 */
                template <std::size_t K, class Fill>
                void emit_late_count_resolver(const std::array<LateCount, K>& counts,
                                              DeviceIndex device, Writes writes, Fill fill,
                                              std::shared_ptr<std::optional<std::uint32_t>>
                                                  registration_token,
                                              std::size_t width = Core::WIDTH_UNKNOWN)
                {
                    Reads reads;
                    bool reads_device = false;
                    for (const LateCount& lc : counts)
                    {
                        if (lc.host || lc.device == nullptr)
                            continue;
                        const Reads slot(*lc.device);
                        reads.keys.insert(reads.keys.end(), slot.keys.begin(), slot.keys.end());
                        reads.lives.insert(reads.lives.end(), slot.lives.begin(), slot.lives.end());
                        reads_device = true;
                    }
                    std::uint32_t* fault = reads_device ? allocate_fault_record(device) : nullptr;

                    observe(reads);
                    observe(writes);
                    specs_.push_back([counts, fault, fill, rr = reads.keys, rw = writes.keys,
                                      registration_token, width](BackingGraph& bg)
                    {
                        std::optional<std::uint32_t> registration;
                        if (registration_token)
                        {
                            if (!registration_token->has_value())
                                *registration_token = bg.reserve_registration();
                            registration = *registration_token;
                        }
                        bg.add_node([counts, fault, fill](sycl::handler& h)
                        {
                            std::array<detail::LiveCountSource, K> sources;
                            for (std::size_t k = 0; k < K; ++k)
                                sources[k] = detail::late_count_source(counts[k]);
                            h.single_task([sources, fault, fill]()
                            {
                                std::array<std::size_t, K> live{};
                                for (std::size_t k = 0; k < K; ++k)
                                    live[k] = detail::read_live_count(sources[k], fault);
                                fill(live);
                            });
                        }, rr, rw, {}, registration);
                        bg.width_last_node(width);
                    });
                    ++task_count_;
                    dirty_ = true;
                }

                /**
                 * @brief Emits an internal node run at its capacity that reads its live width on the device.
                 *
                 * Work-items at or past *@p live_count return at once; @p live_scalar feeds @p fn
                 * one or two words by its arity (docs/design/GRAPH_REPLAY.md §D.2).
                 * @pre The caller's reads name the regions @p live_count and @p live_scalar lie in.
                 * @param width This node's own launch width; stamped where it is added.
                 */
                template <class F>
                void emit_device_sized(std::vector<Core::ResourceRegion> reads,
                                       std::vector<Core::ResourceRegion> writes,
                                       std::size_t capacity, const std::uint32_t* live_count,
                                       const std::uint32_t* live_scalar, F fn,
                                       std::shared_ptr<std::optional<std::uint32_t>>
                                           registration_token,
                                       std::size_t width = Core::WIDTH_UNKNOWN)
                {
                    specs_.push_back([fn, capacity, live_count, live_scalar,
                                      reads = std::move(reads), writes = std::move(writes),
                                      registration_token, width](BackingGraph& bg)
                    {
                        std::optional<std::uint32_t> registration;
                        if (registration_token)
                        {
                            if (!registration_token->has_value())
                                *registration_token = bg.reserve_registration();
                            registration = *registration_token;
                        }
                        bg.add_node([fn, capacity, live_count, live_scalar](sycl::handler& h)
                        {
                            if (capacity == 0)
                            {
                                h.single_task([]() {});
                                return;
                            }
                            h.parallel_for(sycl::range<1>(capacity),
                                           [fn, live_count, live_scalar](sycl::id<1> id)
                            {
                                const std::size_t t = id[0];
                                if (live_count != nullptr && t >= static_cast<std::size_t>(*live_count))
                                    return;
                                if constexpr (std::is_invocable_v<F, std::size_t, std::size_t, std::size_t>)
                                    fn(t, static_cast<std::size_t>(live_scalar[0]),
                                       static_cast<std::size_t>(live_scalar[1]));
                                else if constexpr (std::is_invocable_v<F, std::size_t, std::size_t>)
                                    fn(t, static_cast<std::size_t>(live_scalar[0]));
                                else
                                {
                                    (void)live_scalar;
                                    fn(t);
                                }
                            });
                        }, reads, writes, {}, registration);
                        bg.width_last_node(width);
                    });
                    ++task_count_;
                    dirty_ = true;
                }

                /**
                 * @brief Emits a 1-D per-element task whose size, base and enablement are late-bound.
                 *
                 * The node is compiled once and follows per-step changes without a
                 * recompile; its reads cover the full capacity and any device slot, so
                 * a slot's producer is always a predecessor. See submit_late_window().
                 */
                template <class F>
                Graph& emit_dynamic_per_element(Dynamic dyn,
                                                std::vector<Core::ResourceRegion> reads,
                                                std::vector<Core::ResourceRegion> writes,
                                                std::size_t capacity, F&& fn)
                {
                    require_host_device_slots(dyn);
                    auto lb = std::make_shared<::SushiRuntime::Graph::LateBindings>();
                    adopt_device_bindings(*lb, dyn, reads);
                    std::uint32_t* fault = fault_record_for(dyn);

                    specs_.push_back([fn = std::forward<F>(fn), capacity, lb, fault,
                                      reads = std::move(reads),
                                      writes = std::move(writes)](BackingGraph& bg)
                    {
                        bg.add_node([fn, capacity, lb, fault](sycl::handler& h)
                        {
                            submit_late_window(h, *lb, capacity, fault, fn);
                        }, reads, writes, lb);
                        bg.width_last_node(capacity);
                    });
                    steps_.register_binding_updater(lb, std::move(dyn));
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Rejects a Dynamic whose device count or base slot is not host USM.
                 *
                 * Resolves each slot's owning queue through the context's registry and
                 * requires sycl::usm::alloc::host there (docs/architecture/ARCHITECTURE.md,
                 * "Late-bound device values"). Runs before add() mutates the graph.
                 * @throws Core::SushiException With invalid_access for a non-host slot.
                 */
                void require_host_device_slots(const Dynamic& dyn) const
                {
                    require_host_slot(dyn.device_count, "sized_from_device");
                    require_host_slot(dyn.device_base, "based_at_device");
                }

                /**
                 * @brief Throws unless slot is null or lands in host USM.
                 * @throws Core::SushiException With invalid_access for a slot that is
                 *        not host USM and for one this context never allocated.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                void require_host_slot(const std::uint32_t* slot, const char* what) const
                {
                    if (slot == nullptr)
                        return;
                    void* address = const_cast<void*>(static_cast<const void*>(slot));
                    if (usm_kind_of(address) == sycl::usm::alloc::host)
                        return;
                    const std::optional<void*> base = ctx_->memory().containing_base(address);
                    if (!base)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                            std::string(what) + ": this runtime never allocated the slot, so "
                            "its residency is unknowable; pass a Residency::Host buffer this "
                            "runtime allocated, with the element's index."});
                    if (usm_kind_of(*base) == sycl::usm::alloc::host)
                        return;
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                        std::string(what) + ": the slot lies inside an allocation that is not "
                        "Residency::Host, and the host reads it while kernels may run; pass a "
                        "Residency::Host buffer, with the element's index."});
                }

                /**
                 * @brief Reports which kind of USM an address is, in its owning context.
                 * @param address The address to classify; resolved through the registry.
                 * @return The SYCL allocation kind, or unknown when nothing resolves it.
                 */
                sycl::usm::alloc usm_kind_of(void* address) const
                {
                    try
                    {
                        return sycl::get_pointer_type(
                            address, detail::owning_queue(*ctx_, address).get_context());
                    }
                    catch (const sycl::exception&)
                    {
                        return sycl::usm::alloc::unknown;
                    }
                }

                /**
                 * @brief Copies a Dynamic's device count source onto the bindings and registers it
                 *        as a read.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                static void adopt_device_bindings(::SushiRuntime::Graph::LateBindings& lb,
                                                  const Dynamic& dyn,
                                                  std::vector<Core::ResourceRegion>& reads)
                {
                    if (dyn.device_count != nullptr)
                    {
                        lb.device_count = dyn.device_count;
                        lb.device_count_index = dyn.device_count_index;
                        register_device_slot(dyn.device_count, dyn.device_count_index, reads);
                    }
                    if (dyn.device_base != nullptr)
                    {
                        lb.device_base = dyn.device_base;
                        lb.device_base_index = dyn.device_base_index;
                        register_device_slot(dyn.device_base, dyn.device_base_index, reads);
                    }
                }

                /**
                 * @brief Registers one four-byte slot as a read of the node that consumes it.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                static void register_device_slot(const std::uint32_t* slot, std::size_t index,
                                                 std::vector<Core::ResourceRegion>& reads)
                {
                    reads.push_back(Core::ResourceRegion{
                        const_cast<void*>(static_cast<const void*>(slot)),
                        index * sizeof(std::uint32_t),
                        sizeof(std::uint32_t)});
                }

                /**
                 * @brief Emits a 1-D State update rule with late-bound size and enablement.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <typename T, class F>
                Graph& emit_dynamic_state(Dynamic dyn, State<T>& s, F&& fn)
                {
                    require_host_device_slots(dyn);
                    auto view = steps_.view_for(s);
                    State<T>* sp = &s;
                    auto lb = std::make_shared<::SushiRuntime::Graph::LateBindings>();

                    std::vector<Core::ResourceRegion> reads{ s.read_key() };
                    adopt_device_bindings(*lb, dyn, reads);
                    std::uint32_t* fault = fault_record_for(dyn);

                    specs_.push_back([fn = std::forward<F>(fn), view, sp, lb, fault,
                                      reads = std::move(reads)](BackingGraph& bg)
                    {
                        const std::size_t capacity = sp->size();
                        bg.add_node([fn, view, lb, capacity, fault](sycl::handler& h)
                        {
                            const T* cur = view->cur;
                            T* next = view->next;
                            submit_late_window(h, *lb, capacity, fault,
                                               [fn, cur, next](std::size_t i) { fn(i, cur, next); });
                        }, reads, { sp->write_key() }, lb);
                        bg.width_last_node(capacity);
                    });

                    steps_.register_advance(s, view);
                    steps_.register_binding_updater(lb, std::move(dyn));
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Emits a 1-D offloadable per-element task with a local fallback.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
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

                /**
                 * @brief Builds a fresh backing graph from the recorded specs, compiles it and
                 *        caches the plan.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                void build_plan()
                {
                    // Each rebuild starts a fresh BackingGraph counter at 0.
                    for (auto& token : reduce_registration_tokens_)
                        token->reset();

                    BackingGraph bg(ctx_->offload());
                    for (auto& spec : specs_)
                    {
                        spec.emit(bg);
                        bg.name_last_node(spec.name);
                        spec.registration = bg.last_registration_index();
                    }
                    plan_ = bg.compile();
                    plan_.profiling = profiling_requested_;
                    plan_.capture_base_state();
                    plan_.chain = Execution::classify_chain(plan_, *ctx_);
                    dirty_ = false;
                    ++compiles_;
                }

                /**
                 * @brief Restores the cached plan's per-run state so it can run again.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                void reset_plan_state() noexcept
                {
                    plan_.rearm();
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;

                /**
                 * @brief Holds one recorded add() as an emitter and the name handed to it at build.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                struct NodeSpec
                {
                    // A template parameter keeps lambda to NodeSpec a single implicit conversion.
                    template <class F,
                              std::enable_if_t<!std::is_same<std::decay_t<F>, NodeSpec>::value, int> = 0>
                    NodeSpec(F&& f) : emit(std::forward<F>(f)) {}

                    std::function<void(BackingGraph&)> emit;
                    const char* name = nullptr;
                    std::uint32_t registration = BackingGraph::NO_REGISTRATION;
                };
                std::vector<NodeSpec> specs_;

                /**
                 * @brief Owns the USM scratch of the reduction tasks recorded on this graph.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                std::vector<std::shared_ptr<void>> reduce_scratch_;

                // Registration tokens add_reduce() shares across levels; build_plan() resets them.
                std::vector<std::shared_ptr<std::optional<std::uint32_t>>> reduce_registration_tokens_;

                /**
                 * @brief Holds the liveness observers of every typed handle the recorded tasks
                 *        touch.
                 */
                std::vector<Core::LivenessObserver> observers_;

                /** @brief Owns the per-step lifecycle shared with the DynamicGraph composer. */
                detail::StepMachinery steps_;

                std::size_t task_count_ = 0;

                /**
                 * @brief Tells whether a run_async() step has not been settled yet.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                bool async_step_pending_ = false;

                /**
                 * @brief Counts how many times the backing graph has been compiled.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                std::size_t compiles_ = 0;

                /** @brief Caches the compiled plan, valid while dirty_ is false. */
                ::SushiRuntime::Graph::CompiledGraph plan_;
                bool dirty_ = true;

                // Defaults from RuntimeSettings::profiling; stamped into plan_.profiling at build_plan().
                bool profiling_requested_ = false;
        };

    } // namespace API
} // namespace SushiRuntime
