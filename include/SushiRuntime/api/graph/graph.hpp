/****************************************************************/
/* graph.hpp                                                    */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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

        namespace detail
        {
            // Test-only friend seam onto Graph::plan_ (Graph has no public accessor).
            struct RegistrationIndexTestAccess;
            // Test-only friend seam onto Graph::plan_, for G1 node-event-record tests.
            struct NodeEventRecordsTestAccess;
        }

        /**
         * @brief A run count resolved once per run from exactly one live source.
         *
         * Plans a tiled pass for @c capacity at registration; the live count read
         * each run must not exceed it. Leaving @c host empty and @c device null
         * makes the live count @c capacity itself, matching a fixed-count call.
         * Setting @c host takes precedence over @c device when both are set,
         * which callers should not do; set exactly one.
         *
         * A fixed-count call spells its plain capacity as
         * `LateCount lc; lc.capacity = n;`. Plain brace init (`LateCount{n}`)
         * trips `-Wmissing-field-initializers`, and this type stays an
         * aggregate, so it takes no constructor; the designator form
         * (`LateCount{.capacity = n}`) reads better but is a C++20 feature;
         * on the non-MSVC compiler path this tree builds with (clang++,
         * `-pedantic -Werror`, `cmake/CompilerWarnings.cmake`), it fails as
         * `-Wc++20-designator`, confirmed against that compiler, so it is
         * not offered as the idiom here.
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
             *
             * The one resolution point every verb built on LateCount shares: a host
             * provider wins when set, then a device slot, and with neither the live
             * count is the capacity, so a LateCount left at its default behaves like
             * a fixed count.
             *
             * @param lc The count to resolve.
             * @return The live count, no larger than @c lc.capacity.
             * @throws Core::SushiException With invalid_access if @c lc.device is set
             *         but is not Residency::Host, or invalid_graph if the live count
             *         exceeds @c lc.capacity, the same way an out-of-window based_at()
             *         fails the run.
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
             * @brief Rejects an ill-formed LateCount at registration, not first run.
             *
             * Mirrors require_host_device_slots(): a device slot must be
             * Residency::Host, checked before add() mutates the graph rather than
             * inside the run loop. Also rejects a capacity too wide for the
             * uint32 live-count slots the late-bound verbs resolve into.
             *
             * @param lc   The count to validate.
             * @param what Name of the calling verb, for the error message.
             * @throws Core::SushiException With invalid_access for a non-host
             *         device slot, or invalid_graph for an oversized capacity.
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
                 * @brief Adds a per-element task that participates in no ordering.
                 *
                 * The dependency-blind form of add(Reads, Writes, n, fn): a
                 * per-element callable is void(std::size_t) capturing raw USM
                 * pointers, so nothing in its type says what it touches and the
                 * runtime cannot infer it. This overload therefore records no read or
                 * write regions — the task is both a root and a leaf, ordered against
                 * nothing. Correct for self-contained work (seeding a buffer no other
                 * task in this graph reads, a benchmark kernel); wrong the moment
                 * another task shares data with it. Name the data with
                 * Reads(...)/Writes(...) and use add() whenever ordering matters.
                 *
                 * @param n  Number of elements (the iteration range).
                 * @param fn Callable invoked as fn(i) for each index i.
                 * @return *this, for chaining.
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
                 * @brief Adds an unordered range task; fn receives bounds and handler.
                 *
                 * The dependency-blind counterpart of
                 * add(Reads, Writes, n, range_kernel); see add_untracked(n, fn) for
                 * why the regions cannot be inferred and when this is the right choice.
                 *
                 * @param n  Upper bound; fn is called as fn(0, n, handler).
                 * @param fn Callable invoked as fn(begin, end, sycl::handler&).
                 * @return *this, for chaining.
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
                 * @brief Adds an unordered raw kernel that drives the handler itself.
                 *
                 * The dependency-blind counterpart of add(Reads, Writes, raw_kernel);
                 * see add_untracked(n, fn) for why the regions cannot be inferred and
                 * when this is the right choice.
                 *
                 * @param fn Callable invoked as fn(sycl::handler&).
                 * @return *this, for chaining.
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
                 * @brief Adds an unordered multidimensional task over an Extent.
                 *
                 * The 2-D/3-D counterpart of add_untracked(std::size_t, fn): the
                 * runtime submits a parallel_for over the Extent's box and invokes
                 * @p fn with the global sycl::id of each work-item. When the Extent
                 * carries an origin offset the iteration covers only that sub-box, so
                 * a kernel can skip a halo or touch just a ghost layer. Plain 1-D
                 * counts keep using the std::size_t overloads; pass a 1-D Extent only
                 * when you want an origin offset. Records no regions — use
                 * add(Reads, Writes, Extent, fn) when the task must be ordered.
                 *
                 * @tparam N  Iteration rank (1, 2, or 3).
                 * @param e   The iteration box (extent and origin offset).
                 * @param fn  Callable invoked as fn(sycl::id<N>) per work-item.
                 * @return *this, for chaining.
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
                 * @brief Adds an unordered tiled task over an NdRange.
                 *
                 * Submits a parallel_for over the NdRange and hands @p fn a
                 * sycl::nd_item<N>, exposing the work-group model — barriers, local
                 * (shared) memory, sub-groups — that a flat parallel_for cannot. Use
                 * it for blocked algorithms (tiled GEMM, shared-memory stencils)
                 * where neighbouring work-items must cooperate. Records no regions —
                 * use add(Reads, Writes, NdRange, fn) when the task must be ordered.
                 *
                 * @tparam N  Iteration rank (1, 2, or 3).
                 * @param r   Global and local ranges; global must tile by local.
                 * @param fn  Callable invoked as fn(sycl::nd_item<N>) per work-item.
                 * @return *this, for chaining.
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
                 * @brief Adds a tiled NdRange task with handle-based tracking.
                 *
                 * The ordered counterpart of add_untracked(NdRange, fn): the same
                 * work-group execution model — barriers, local memory, sub-groups —
                 * but the task is ordered against the rest of the graph by the
                 * regions it declares, so a tiled kernel is no longer forced to give
                 * up dependency tracking to get an nd_item.
                 *
                 * @tparam N  Iteration rank (1, 2, or 3).
                 * @param reads  Handles this task reads (their read keys).
                 * @param writes Handles this task writes (their write keys).
                 * @param r   Global and local ranges; global must tile by local.
                 * @param fn  Callable invoked as fn(sycl::nd_item<N>) per work-item.
                 * @return *this, for chaining.
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
                 * @brief Adds a range task with handle-based tracking.
                 *
                 * The ordered counterpart of add_untracked(n, range_kernel): @p fn
                 * still drives the sycl::handler over [0, n) itself — the escape
                 * hatch for a kernel the per-element form cannot express — but the
                 * regions it declares order it against the rest of the graph.
                 *
                 * @param reads  Handles this task reads (their read keys).
                 * @param writes Handles this task writes (their write keys).
                 * @param n      Upper bound; fn is called as fn(0, n, handler).
                 * @param fn     Callable invoked as fn(begin, end, sycl::handler&).
                 * @return *this, for chaining.
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
                 * @brief Adds a raw kernel with handle-based tracking.
                 *
                 * The ordered counterpart of add_untracked(raw_kernel): @p fn owns
                 * the whole command group — its own parallel_for, local accessors,
                 * a memcpy — while the declared regions still order it. Reach for it
                 * when no other overload's launch shape fits; prefer the shaped
                 * overloads otherwise, since they cannot get the launch geometry and
                 * the declared data out of step.
                 *
                 * @param reads  Handles this task reads (their read keys).
                 * @param writes Handles this task writes (their write keys).
                 * @param fn     Callable invoked as fn(sycl::handler&).
                 * @return *this, for chaining.
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
                 * sized_from_device() takes the count from a slot a predecessor
                 * kernel wrote instead of from the host, which is what lets a
                 * produce-then-consume chain (broadphase to narrowphase to solver)
                 * run in one composition with no worst-case launch width. The counter
                 * is registered as one of this task's reads, so the producer is
                 * necessarily ordered before it.
                 *
                 * based_at() / based_at_device() move where the node starts, for a
                 * slice whose offset shifts between steps — a graph colour, say. The
                 * callable still receives absolute indices, and the window must fit
                 * the capacity: one that does not fails the run rather than reading
                 * past the end.
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
                 * @param fn   Nullary callable run once per step; must not submit SYCL work.
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
                 * @brief Names the task most recently added to this graph.
                 *
                 * Without a name a node reports as "unnamed_task" in
                 * RunReport::NodeTiming, which makes a profile of a real graph a list
                 * of identical labels. Chain this onto any add() to fix that:
                 *
                 * @code
                 * sim.add(API::Reads(a), API::Writes(b), n, integrate).name_last("integrate");
                 * @endcode
                 *
                 * A trailing name argument would have been shorter, but two launch
                 * shapes end in a parameter pack whose last element is the callable —
                 * add(Extent, captures..., fn) and add(State, States..., fn) — so a
                 * defaulted trailing name cannot be expressed there. One spelling that
                 * works for every shape, including any added later, beats a trailing
                 * argument on most of them and an exception for the rest.
                 *
                 * For a task that expands to several nodes, such as a reduction, this
                 * names the last one — the final fold.
                 *
                 * @param name Persistent label, stored not copied; a string literal is
                 *             the intended use. Ignored when no task has been added.
                 * @return *this, for chaining.
                 */
                Graph& name_last(const char* name) noexcept
                {
                    if (!specs_.empty())
                        specs_.back().name = name;
                    return *this;
                }

                /**
                 * @brief Adds a reduction whose combination order never varies.
                 *
                 * Reduces the first @p n elements of @p input into @p output[0] with
                 * a result that is bit-identical across runs, worker counts, devices
                 * and steal patterns — the property a replayable simulation needs
                 * from every accumulation that feeds its state, and the one a
                 * schedule-driven parallel reduce cannot give.
                 *
                 * It works by folding fixed-size tiles: each work-item combines a
                 * contiguous run of at most 256 values left to right, the partials
                 * are folded the same way, and that repeats until one value remains.
                 * Which values meet which therefore depends only on @p n and that
                 * tile size — never on how the work was scheduled. The intermediate
                 * levels use scratch this graph allocates and owns, on the same
                 * device and with the same residency as @p input.
                 *
                 * @tparam T       Element type.
                 * @tparam Combine Binary functor; must be trivially copyable so it can
                 *                 be captured into a device kernel.
                 * @param input    Values to reduce; must hold at least @p n elements.
                 * @param output   Destination; element 0 receives the result.
                 * @param n        Number of leading elements of @p input to reduce.
                 * @param op       The combiner, e.g. Sum<float>{}.
                 * @param identity Result when @p n is zero; never enters the
                 *                 arithmetic otherwise.
                 * @return *this, for chaining.
                 * @throws Core::SushiException If @p n exceeds @p input's size, if
                 *         @p output is empty, or if scratch cannot be allocated.
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
                        // Nothing to fold: the identity is the whole answer. Still a
                        // real node, so downstream readers order after it as usual.
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
                 * @brief Adds a fixed-order reduction using the combiner's identity.
                 *
                 * The shorthand for the built-in combiners (Sum, Minimum, Maximum),
                 * which each declare the value their reduction produces over no
                 * elements. A combiner without a static identity() — a lambda, say —
                 * must use the overload that takes the identity explicitly.
                 *
                 * @tparam T       Element type.
                 * @tparam Combine Binary functor exposing a static identity().
                 * @param input  Values to reduce; must hold at least @p n elements.
                 * @param output Destination; element 0 receives the result.
                 * @param n      Number of leading elements of @p input to reduce.
                 * @param op     The combiner, e.g. Sum<float>{}.
                 * @return *this, for chaining.
                 * @throws Core::SushiException As the five-argument overload.
                 */
                template <typename T, class Combine>
                Graph& add_reduce(const Buffer<T>& input, const Buffer<T>& output,
                                  std::size_t n, Combine op)
                {
                    return add_reduce(input, output, n, op, Combine::identity());
                }

                /**
                 * @brief Adds a reduction whose live element count is late-bound.
                 *
                 * Plans the same fixed-tile tree as the @p std::size_t overload, but
                 * for @p n.capacity, and resolves the live count once per run instead
                 * of once at registration. The fold order for a live count that ever
                 * arrives is exactly the order the fixed-count overload would produce
                 * for that same count (spec 2026-09-16-scan-and-counting-sort-design.md
                 * A1): every level beyond the one where the live chain reaches one
                 * value degenerates to a single-element pass-through, so results stay
                 * byte-identical whichever way the count was bound.
                 *
                 * @tparam T       Element type.
                 * @tparam Combine Binary functor; must be trivially copyable.
                 * @param input    Values to reduce; must hold at least @p n.capacity elements.
                 * @param output   Destination; element 0 receives the result.
                 * @param n        Capacity plus the live count's source.
                 * @param op       The combiner, e.g. Sum<float>{}.
                 * @param identity Result when the live count is zero.
                 * @return *this, for chaining.
                 * @throws Core::SushiException If @p n.capacity exceeds @p input's
                 *         size, if @p output is empty, or at run if the live count
                 *         exceeds @p n.capacity.
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
                        // Capacity zero: every run's live count is zero too, so the
                        // identity is the whole answer, always.
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

                    // One token shared by every node this call emits (resolver, every
                    // level, the trailing zero-count write), as the fixed-count
                    // overload shares one across its levels (G1 spec §F5).
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
                 * @brief Adds a late-bound reduction using the combiner's identity.
                 * @tparam T       Element type.
                 * @tparam Combine Binary functor exposing a static identity().
                 * @param input  Values to reduce; must hold at least @p n.capacity elements.
                 * @param output Destination; element 0 receives the result.
                 * @param n      Capacity plus the live count's source.
                 * @param op     The combiner, e.g. Sum<float>{}.
                 * @return *this, for chaining.
                 * @throws Core::SushiException As the five-argument overload.
                 */
                template <typename T, class Combine>
                Graph& add_reduce(const Buffer<T>& input, const Buffer<T>& output,
                                  LateCount n, Combine op)
                {
                    return add_reduce(input, output, n, op, Combine::identity());
                }

                /**
                 * @brief Reduces each segment of a ragged array, in a fixed order.
                 *
                 * The scatter-accumulate shape a simulation actually needs: per-body
                 * contact impulses, per-vertex contributions from the elements
                 * sharing that vertex. @p segment_offsets is the usual CSR-style
                 * boundary array of @p segment_count + 1 entries, so segment s covers
                 * values [offsets[s], offsets[s+1]) and @p output[s] receives its
                 * fold.
                 *
                 * One work-item per segment folds that segment left to right, so
                 * there are no atomics and no cross-work-item combining and the
                 * result is reproducible by construction. The trade-off is load
                 * balance: a segment of ten thousand values and a segment of one cost
                 * one work-item each. The caller owns the layout, and determinism is
                 * the contract, so the imbalance is left visible rather than traded
                 * away for a split that would break the ordering guarantee.
                 *
                 * @tparam T       Element type of the values and the result.
                 * @tparam Combine Binary functor; must be trivially copyable.
                 * @param values          The concatenated segment contents.
                 * @param segment_offsets Boundaries; must hold @p segment_count + 1
                 *                        entries, non-decreasing, the last no larger
                 *                        than @p values' size.
                 * @param output          Destination; must hold @p segment_count entries.
                 * @param segment_count   Number of segments to reduce.
                 * @param op              The combiner, e.g. Sum<float>{}.
                 * @param identity        Result for an empty segment.
                 * @return *this, for chaining.
                 * @throws Core::SushiException If @p segment_offsets or @p output is
                 *         too small for @p segment_count.
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
                 * @brief Reduces each segment using the combiner's identity.
                 *
                 * The shorthand form; see the overload taking an explicit identity for
                 * the layout contract and the load-balance trade-off.
                 *
                 * @tparam T       Element type of the values and the result.
                 * @tparam Combine Binary functor exposing a static identity().
                 * @param values          The concatenated segment contents.
                 * @param segment_offsets Boundaries; @p segment_count + 1 entries.
                 * @param output          Destination; @p segment_count entries.
                 * @param segment_count   Number of segments to reduce.
                 * @param op              The combiner, e.g. Sum<float>{}.
                 * @return *this, for chaining.
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
                 *
                 * One work-item per segment, planned for @p segment_count.capacity and
                 * resolved once per run; a live segment count below capacity simply
                 * launches fewer work-items, so the fold order per segment is
                 * unaffected and byte-identical to the fixed-count overload for that
                 * same live count.
                 *
                 * @tparam T       Element type of the values and the result.
                 * @tparam Combine Binary functor; must be trivially copyable.
                 * @param values          The concatenated segment contents.
                 * @param segment_offsets Boundaries; @p segment_count.capacity + 1 entries.
                 * @param output          Destination; @p segment_count.capacity entries.
                 * @param segment_count   Capacity plus the live segment count's source.
                 * @param op              The combiner, e.g. Sum<float>{}.
                 * @param identity        Result for an empty segment.
                 * @return *this, for chaining.
                 * @throws Core::SushiException If @p segment_offsets or @p output is
                 *         too small for @p segment_count.capacity, or at run if the
                 *         live count exceeds it.
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
                 * @brief Reduces each segment, late-bound, using the combiner's identity.
                 * @tparam T       Element type of the values and the result.
                 * @tparam Combine Binary functor exposing a static identity().
                 * @param values          The concatenated segment contents.
                 * @param segment_offsets Boundaries; @p segment_count.capacity + 1 entries.
                 * @param output          Destination; @p segment_count.capacity entries.
                 * @param segment_count   Capacity plus the live segment count's source.
                 * @param op              The combiner, e.g. Sum<float>{}.
                 * @return *this, for chaining.
                 * @throws Core::SushiException As the six-argument overload.
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
                 *
                 * Plans add_reduce()'s 256-tile tree over @p n.capacity, up-sweeps it
                 * exactly as add_reduce() does but keeps every level's tile sums
                 * instead of discarding them, then down-sweeps from the top: each
                 * level's tile hands its running base to the level below, which walks
                 * its own tile left to right emitting the exclusive running sum. Level
                 * 0's down-sweep writes @p offsets directly, and the top pass's sum is
                 * copied into both @p total and `offsets[n]`, so a CSR consumer never
                 * needs a second read. No host step runs between the two sweeps.
                 *
                 * @param counts  Values to scan; must hold at least @p n.capacity elements.
                 * @param offsets Destination; must hold at least @p n.capacity + 1 elements.
                 * @param total   Destination for the grand sum; must hold at least 1 element.
                 * @param n       Capacity plus the live count's source.
                 * @return *this, for chaining.
                 * @throws Core::SushiException If a buffer is too small, if @p counts
                 *         and @p offsets sit on different devices, or at run if the
                 *         live count exceeds @p n.capacity.
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

                    // A single level (capacity 1..256) needs neither scratch: the
                    // one down-sweep pass reads counts and writes offsets directly.
                    // Guarded exactly as add_reduce() guards its own scratch, since
                    // sycl::malloc_host(0, q) returns null and the allocator treats
                    // a null result as a failure.
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

                    // state[i] is level i's live in_count, state[level_count+i] its
                    // live out_count, as add_reduce()'s resolver fills them.
                    const std::size_t state_words = 2 * level_count;
                    std::shared_ptr<std::uint32_t> state =
                        detail::make_reduce_scratch<std::uint32_t>(
                            ctx_, state_words, counts.device_index(), counts.residency());
                    reduce_scratch_.push_back(std::static_pointer_cast<void>(state));
                    std::uint32_t* state_ptr = state.get();
                    const Core::ResourceRegion state_region{
                        state_ptr, 0, state_words * sizeof(std::uint32_t)};

                    // One token shared by every node this call emits: the resolver,
                    // every up level, every down level and the closing write.
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

                    // Up-sweep: the same tile fold add_reduce() runs over every level
                    // but the top, writing each level's tile sums into scratch for
                    // the down-sweep to read as its bases; the top level's own sum is
                    // never folded here, because the down-sweep's top pass walks it
                    // directly and computes that sum as a side effect of doing so.
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

                    // Down-sweep, top to bottom: the top level has no level above it,
                    // so it scans its own (<= 256-value) tile sequentially from a base
                    // of zero, which is also the grand sum, so it always runs (an
                    // empty live count still has to publish total = 0).
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

                        // General level: each tile's base is the down-swept exclusive
                        // prefix the level above already produced for it.
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

                    // Closing write: level 0's down-sweep only reaches offsets[live n
                    // - 1]; the top pass already settled total, so this copies it into
                    // offsets[live n] without re-deriving it.
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
                 * @brief Stably argsorts @p keys by 8-bit digits into bucket offsets.
                 *
                 * See docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-design.md
                 * §A3 for the algorithm, the digit count and the offsets/counts derivation.
                 *
                 * @param keys     Values to sort by; must hold at least @p n.capacity elements.
                 * @param indices  Destination for the stable sort order; must hold at
                 *                 least @p n.capacity elements.
                 * @param offsets  Destination exclusive bucket starts; must hold at
                 *                 least @p key_range.capacity + 1 elements.
                 * @param total    Destination for the live element count; must hold at
                 *                 least 1 element.
                 * @param n        Capacity plus the live element count's source.
                 * @param key_range Capacity plus the live bucket count's source.
                 * @param counts   Optional per-bucket counts; must hold at least
                 *                 @p key_range.capacity elements when not null.
                 * @return *this, for chaining.
                 * @throws Core::SushiException If a buffer is too small, if @p keys,
                 *         @p indices, @p offsets or @p counts sit on different
                 *         devices, if @p key_range.capacity is zero, or at run if
                 *         either live count exceeds its capacity.
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

                    // The digit plan: per digit, its live work counts (zero past the
                    // live key range's digits) and whether it places into the indices.
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

                        // Every digit after the first reads whichever of indices and
                        // scratch the previous one filled, chosen at run from the plan.
                        Reads sources = first ? Reads()
                                              : (scratch_base != nullptr
                                                    ? Reads(indices_region, scratch_region)
                                                    : Reads(indices_region));

                        // Histogram: one work-item per tile, writing (not
                        // accumulating) its own row, so no run needs to clear it.
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

                        // Column scan: one work-item per bin, sequential over its
                        // tiles, so bins stay independent and every column is a plain
                        // left-to-right exclusive scan.
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

                        // Bin base: one work-item, sequential over the (always full)
                        // 256 bin totals, since the digit range never shrinks with n.
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

                        // Placement: one work-item per tile, walking its elements in
                        // ascending index order and writing them into the base each
                        // bin already owns for this tile, which is what keeps the
                        // scatter stable.
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

                    // Offsets: one work-item per bucket (plus the sentinel), a plain
                    // lower_bound over the sorted keys; no live bucket count is read
                    // here, since a search past it still lands on the live element
                    // count and the sentinel falls out for free (design doc §A3).
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

                    // Counts: the difference of two already-settled offsets, so it
                    // needs no live scalar of its own, key range included: past the
                    // live range both neighbours are the live element count and the
                    // difference is zero, which is the correct bucket count there too.
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

                /**
                 * @brief Reports how much work has been recorded.
                 * @return Number of update rules (tasks) added so far.
                 */
                std::size_t size() const noexcept { return task_count_; }

                /** @brief Stands for "this rule has no index yet". */
                static constexpr std::uint32_t no_registration = BackingGraph::NO_REGISTRATION;

                /**
                 * @brief Returns the registration index the rule at @p rule took.
                 *
                 * RunReport numbers a node event by registration index, and the
                 * several nodes one add() lowers to share one index, so this is how
                 * a caller maps a record back to the call that recorded it. The
                 * indices are drawn when the graph compiles, so every rule reports
                 * @ref no_registration until the first run, and a recompile may
                 * renumber them.
                 *
                 * @param rule Position in add() order, below @ref size().
                 * @return The index, or @ref no_registration when @p rule is out of
                 *         range or the graph has not compiled yet.
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
                 * @brief Requests or withdraws per-node device timing for this graph.
                 *
                 * Marks the plan dirty so the next run recompiles with the new
                 * `CompiledGraph::profiling` flag stamped in. Independent of every
                 * other graph on the same runtime.
                 * @param requested True to dispatch this graph's device nodes on the
                 *                  runtime's profiling queue and collect NodeTiming.
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
                 * @brief Runs the graph once into a report the caller owns.
                 *
                 * The allocation-free form of run(). A fixed-step loop keeps one
                 * report alive and passes it every tick; the report is reset and
                 * refilled in place, so after the first run the per-run vectors are
                 * reused rather than reallocated:
                 *
                 * ```cpp
                 * Core::RunReport report;
                 * while (running)
                 *     simulation.run(report);
                 * ```
                 *
                 * Identical to run() in every other respect. The steady state is
                 * allocation-free only with profiling off — a profiling run rebuilds
                 * its per-node labels by design.
                 *
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A reference to @p reuse, so the call still reads as a run
                 *         that produced a report.
                 * @throws Core::SushiException on any synchronous or device error.
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
                 * @brief Starts one step and returns without waiting for it.
                 *
                 * The asynchronous counterpart of run(). The step is handed to the
                 * worker pool and this returns immediately, so the caller can do
                 * work that needs no workers — extracting the previous step's state
                 * for a renderer or an audio system — while this one runs, then
                 * settle up through the returned handle:
                 *
                 * ```cpp
                 * auto step = simulation.run_async();
                 * extract_previous_frame();
                 * Core::RunReport report = step.wait();
                 * ```
                 *
                 * One step only: the multi-step drivers (run(n), run_for, run_until)
                 * stay synchronous, because their per-step host work has to happen
                 * between steps and there is nothing for the caller to overlap with
                 * in the middle of a batch. A step is one tick, which is the
                 * granularity a frame actually wants.
                 *
                 * The handle must not outlive this graph, and its destructor waits
                 * for the step rather than letting the plan be destroyed underneath
                 * the workers — see RunHandle.
                 *
                 * @return A handle for the step now in flight.
                 * @throws Core::SushiException If the graph has no tasks, references
                 *         a freed Buffer/State, the engine does not support
                 *         asynchronous runs, or a run is already in progress.
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

                    // Same pre-step order as the synchronous driver: point the State
                    // views at their buffers, clear a previous run's cancellation,
                    // zero the profiling accumulators, then refresh the late-bound
                    // controls. Diverging here would make an asynchronous step mean
                    // something subtly different from a synchronous one.
                    steps_.resync();
                    engine.reset_cancellation();
                    if (plan_.profiling)
                        engine.reset_profiling(plan_);

                    state.before = engine.metrics();
                    state.device_to_host_before = ctx_->device_to_host_bytes();
                    state.host_to_device_before = ctx_->host_to_device_bytes();
                    state.task_count = task_count_;

                    steps_.before_step();
                    reset_plan_state();
                    state.started = clock::now();
                    engine.submit(plan_);

                    async_step_pending_ = true;
                    return RunHandle<Graph>(this, state);
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
                // A Region IS a recorder: it derives from Graph to inherit every
                // add() overload, so it needs the protected-by-friendship ctor and
                // access to the recorded specs/updaters. The DynamicGraph composer
                // reads those same members to build one combined plan.
                friend class Region;
                friend class DynamicGraph;
                // The handle run_async() returns settles the step through the two
                // private members below, which is the whole of its access.
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
                 * @brief Closes out an asynchronous step once the engine reports done.
                 *
                 * The tail of drive_impl's loop body plus its report building, split
                 * out because those happen in a different call from the submission.
                 * Advancing every State here — not at submit — is what makes an
                 * asynchronous step observationally identical to a synchronous one:
                 * the just-written field becomes current only after the step has
                 * genuinely finished.
                 *
                 * @param state  What run_async() recorded when it submitted the step.
                 * @param report The report to fill; reset before it is written.
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
                    report.device_to_host_bytes =
                        ctx_->device_to_host_bytes() - state.device_to_host_before;
                    report.host_to_device_bytes =
                        ctx_->host_to_device_bytes() - state.host_to_device_before;

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
                 * @brief The same driver, writing into a caller-owned report.
                 *
                 * Every run entry funnels through here — the value-returning drive()
                 * above is a thin adapter over it — so the reuse overloads cannot
                 * drift from the allocating ones.
                 *
                 * @param keep_going Called with the count of completed steps before
                 *                   each step; the loop continues while it is true.
                 * @param report     The report to fill; reset before the run starts.
                 * @return A pointer to @p report on success, or a mapped Error.
                 */
                template <class KeepGoing>
                Core::Result<Core::RunReport*, Core::Error>
                drive(KeepGoing keep_going, Core::RunReport& report,
                     std::size_t expected_invocations = 1,
                     StepSync sync = StepSync::EveryStep) noexcept
                {
                    // Before any early return. A caller reusing one report across
                    // ticks must never read the previous run's numbers back out of a
                    // run that failed; fill_run_report sets the flag back on success.
                    report.reset();
                    report.is_successful = false;

                    if (task_count_ == 0)
                        return Core::make_error(Core::Error{Core::Errc::invalid_graph,
                                                            "Graph has no tasks to run."});
                    // Before anything touches the plan. A synchronous run re-arms
                    // every node's in-degree, so letting one start while an
                    // asynchronous step is still executing would rewrite the
                    // scheduling state of the DAG the workers are walking — a much
                    // worse failure than the engine's own reentrancy rejection,
                    // which only fires later, after the damage.
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

                    // Point every State view at its current/next buffers before the
                    // first step. Covers both a fresh build and a replay continuing
                    // from a previous run.
                    steps_.resync();

                    auto& engine = ctx_->get_execution_engine();

                    // Start each run with a clean cancellation flag so a cancel that
                    // ended a previous run does not abort this one.
                    engine.reset_cancellation();

                    // Clear the profiling accumulators so the report covers only this
                    // run; only when the plan requests profiling.
                    const bool profiling = plan_.profiling;
                    if (profiling)
                        engine.reset_profiling(plan_, expected_invocations);

                    const auto before = engine.metrics();
                    const std::size_t d2h_before = ctx_->device_to_host_bytes();
                    const std::size_t h2d_before = ctx_->host_to_device_bytes();
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

                    // Explicit host<->device transfers issued during this run window
                    // (selective readback). Implicit USM migration is not counted.
                    report.device_to_host_bytes = ctx_->device_to_host_bytes() - d2h_before;
                    report.host_to_device_bytes = ctx_->host_to_device_bytes() - h2d_before;

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
                        // Re-point any double-buffered offload node's live IO and
                        // refresh late-bound node controls (size, enablement) for this
                        // step before replaying the compiled plan (no-op otherwise).
                        steps_.before_step();

                        reset_plan_state();
                        engine.execute(plan_); // lvalue -> non-consuming replay
                        steps_.check_faults();

                        // Advance every State after the step so its just-written
                        // next field becomes current — both for the following step
                        // and for the caller reading current() once the run ends.
                        steps_.after_step();
                        ++executed;
                    }
                    return executed;
                }

                /**
                 * @brief Runs the steps as an in-order chain from this thread.
                 *
                 * The per-step host work runs between submissions exactly as on the
                 * scheduler; it only moves host pointers and host-set controls, so
                 * it never needs the device to have finished (spec §C, layer 1).
                 * @param keep_going Called with the count of submitted steps before each step.
                 * @param engine     Consulted only for cancellation.
                 * @param sync       AtEnd for a fixed step count, EveryStep when the stop test may read results.
                 * @return The number of steps run.
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

                /**
                 * @brief Emits a 1-D per-element tracked task, the single funnel
                 *        for the raw-pointer and Reads/Writes tracking overloads.
                 *
                 * @param registration_token Shared slot grouping this node's index
                 *        with others from one public add() call; null draws a plain
                 *        fresh index.
                 * @param width This node's own launch width; stamped where it is added.
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

                // The half-open element window a late-bound node iterates this step:
                // where it starts and how many elements it covers. The two are
                // resolved and validated together, because only their combination
                // says whether the node stays inside the buffers it was compiled for.
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

                // Submits a late-bound node's kernel over its live size. A step with
                // nothing to do is ordinary (a tick with no contacts), and a
                // zero-width parallel_for is not portable across SYCL backends, so an
                // empty count submits an empty kernel instead: the node still
                // completes and still releases its successors.
                template <class F>
                static void submit_live_range(sycl::handler& h, LiveRange range, const F& fn)
                {
                    if (range.count == 0)
                    {
                        h.single_task([]() {});
                        return;
                    }
                    // The offset is applied here rather than by the caller, so a
                    // kernel written against absolute indices stays correct whether
                    // or not its node happens to carry a base.
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
                    const std::optional<void*> base = ctx_->containing_usm_base(address);
                    const int queue = ctx_->context_index_for_ptr(base ? *base : address);
                    return allocate_fault_record(
                        DeviceIndex{static_cast<std::size_t>(queue < 0 ? 0 : queue)});
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
                 * @brief Throws unless @p slot is null or lands in host USM.
                 *
                 * Asks the slot's own address first, then, when that answers anything
                 * but host, resolves it to the allocation containing it and asks
                 * again: a caller offsetting into a plan array hands over an interior
                 * pointer, which SYCL reports as unknown. Runs while add() builds a
                 * node, so the containment search never meets a dispatch.
                 * @param slot The slot pointer to check; null passes.
                 * @param what Name of the control that set it, for the message.
                 * @throws Core::SushiException With invalid_access for a slot that is
                 *         not host USM and for one this context never allocated.
                 */
                void require_host_slot(const std::uint32_t* slot, const char* what) const
                {
                    if (slot == nullptr)
                        return;
                    void* address = const_cast<void*>(static_cast<const void*>(slot));
                    if (usm_kind_of(address) == sycl::usm::alloc::host)
                        return;
                    const std::optional<void*> base = ctx_->containing_usm_base(address);
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
                            address, ctx_->get_queue_for_ptr(address).get_context());
                    }
                    catch (const sycl::exception&)
                    {
                        return sycl::usm::alloc::unknown;
                    }
                }

                // Copies a Dynamic's device count source onto the node's bindings and
                // registers that slot as a read. Doing the registration here rather
                // than asking the caller for it is what makes "the producer runs
                // first" a property of the graph instead of a documented convention:
                // the read region orders this node behind whoever writes the count.
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

                // Names one four-byte slot as a read of the node that will consume it.
                // Just the one element, not the whole buffer, so two nodes reading
                // different slots out of one buffer stay unordered against each other.
                static void register_device_slot(const std::uint32_t* slot, std::size_t index,
                                                 std::vector<Core::ResourceRegion>& reads)
                {
                    reads.push_back(Core::ResourceRegion{
                        const_cast<void*>(static_cast<const void*>(slot)),
                        index * sizeof(std::uint32_t),
                        sizeof(std::uint32_t)});
                }

                // Emits a 1-D State update rule with late-bound size and enablement.
                // Mirrors emit_dynamic_per_element but reads the State's current/next
                // pointers fresh each submit (following the swap) and registers the
                // once-per-step advance, so a stepped field can vary its active count
                // or sleep entirely with no recompile.
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
                    // Each rebuild starts a fresh BackingGraph counter at 0.
                    for (auto& token : reduce_registration_tokens_)
                        token->reset();

                    BackingGraph bg(*ctx_);
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
                // Recorded as {emitter, name} rather than a bare callable: the name is
                // handed to the emitter when the plan is built, not captured when the
                // task is recorded, which is what lets name_last() attach one after the
                // add() call has already returned.
                struct NodeSpec
                {
                    // A template rather than a std::function parameter: taking the
                    // function type directly would make lambda -> std::function ->
                    // NodeSpec two user-defined conversions, and only one is allowed
                    // implicitly, so every push_back below would need spelling out.
                    template <class F,
                              std::enable_if_t<!std::is_same<std::decay_t<F>, NodeSpec>::value, int> = 0>
                    NodeSpec(F&& f) : emit(std::forward<F>(f)) {}

                    std::function<void(BackingGraph&)> emit;
                    const char* name = nullptr;
                    std::uint32_t registration = BackingGraph::NO_REGISTRATION;
                };
                std::vector<NodeSpec> specs_;

                // USM scratch owned by the reduction tasks recorded on this graph.
                // Allocated when the task is recorded, not when the plan compiles, so
                // the pointer its kernels captured stays valid across a recompile and
                // across the DynamicGraph composer rebuilding a region from its specs.
                std::vector<std::shared_ptr<void>> reduce_scratch_;

                // Registration tokens add_reduce() shares across levels; build_plan() resets them.
                std::vector<std::shared_ptr<std::optional<std::uint32_t>>> reduce_registration_tokens_;

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

                // True between run_async() and the settling of its handle. Guards
                // the plan against a second run re-arming nodes the workers are
                // still walking; see drive().
                bool async_step_pending_ = false;

                // Number of times the backing graph has been compiled. A late-bound
                // graph proves its value by holding this at 1 across a multi-step run
                // whose sizes and enable flags change every step.
                std::size_t compiles_ = 0;

                // Cached compiled plan, valid while !dirty_; replayed across steps.
                // It owns its own replay baseline (see capture_base_state/rearm).
                Execution::CompiledGraph plan_;
                bool dirty_ = true;

                // Defaults from RuntimeSettings::profiling; stamped into plan_.profiling at build_plan().
                bool profiling_requested_ = false;
        };

    } // namespace API
} // namespace SushiRuntime
