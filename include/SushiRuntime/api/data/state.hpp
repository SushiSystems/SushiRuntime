/****************************************************************/
/* state.hpp                                                    */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>
#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Indirection a stepped kernel reads to find its data each step.
             *
             * A State kernel captures a shared_ptr to one of these instead of raw
             * pointers. Because the kernel's host-side body runs once per step (at
             * submit time), it reads cur/next fresh every step. The runtime updates
             * these two fields when it swaps the State between steps, so one
             * compiled node follows the double-buffer without being recompiled.
             *
             * @tparam T Element type of the buffers this view points into.
             */
            template <typename T>
            struct StateView
            {
                T* cur = nullptr;   /**< Current-state base pointer for this step. */
                T* next = nullptr;  /**< Next-state base pointer for this step. */
            };
        } // namespace detail

        /**
         * @brief A time-evolving field the runtime double-buffers across steps.
         *
         * Many stepped workloads (stencils, PDE solvers, the digital-twin driver)
         * must read the current state and write the next one — they cannot update
         * in place. A State owns two equally sized buffers, current() and next(),
         * and the runtime swaps their roles after each step (advance()), an O(1)
         * pointer exchange. The user writes one step as "read current, write next"
         * and never manages the ping-pong.
         *
         * @tparam T Element type. Must be trivially copyable for device use.
         */
        template <typename T>
        class State
        {
            public:
                /** @brief Element type, exposed so generic code can recover it. */
                using value_type = T;

                /** @brief Constructs an empty state that owns no memory. */
                State() = default;

                /**
                 * @brief Allocates both buffers of @p count elements on one device.
                 *
                 * Both fields land on @p device_index, so a step kernel that reads
                 * current and writes next stays within one context.
                 *
                 * @param ctx          The runtime context that owns the allocations.
                 * @param count        Number of elements in each buffer.
                 * @param device_index Target device queue for both buffers.
                 * @param residency    Shared, Device or Host; see Residency.
                 */
                State(std::shared_ptr<Execution::RuntimeContext> ctx,
                      std::size_t count, DeviceIndex device_index = DeviceIndex{0},
                      Residency residency = Residency::Shared)
                    : cur_(ctx, count, device_index, residency),
                      next_(std::move(ctx), count, device_index, residency)
                {
                }

                /**
                 * @brief Exposes the field a step kernel reads from.
                 * @return Pointer to the first element of the current field.
                 */
                T* current() const noexcept { return cur_.data(); }

                /**
                 * @brief Exposes the field a step kernel writes into.
                 * @return Pointer to the first element of the next field.
                 */
                T* next() const noexcept { return next_.data(); }

                /**
                 * @brief Reports the capacity of the pair.
                 * @return Number of elements in each field; both are the same size.
                 */
                std::size_t size() const noexcept { return cur_.size(); }

                /**
                 * @brief Tells whether this State still owns memory.
                 * @return True if it owns no allocation, e.g. after being moved from.
                 */
                bool empty() const noexcept { return cur_.empty(); }

                /**
                 * @brief Identifies the region a task reading this State depends on.
                 * @return The current field's base address, as used by the tracker.
                 */
                void* read_key() const noexcept { return cur_.dependency_key(); }

                /**
                 * @brief Identifies the region a task writing this State depends on.
                 * @return The next field's base address, as used by the tracker.
                 */
                void* write_key() const noexcept { return next_.dependency_key(); }

                /**
                 * @brief A weak observer of this State's allocation lifetime.
                 *
                 * Mirrors Buffer::liveness(): a graph that records this State holds the
                 * observer to detect a State freed before a run. The current field's
                 * token stands for the State — both fields are freed together with it.
                 *
                 * @return A weak observer of the State's allocations, empty when empty.
                 */
                Core::LivenessObserver liveness() const noexcept { return cur_.liveness(); }

                /**
                 * @brief Host-side access to the current field's elements (shared only).
                 *
                 * Delegates to the current buffer, so a device-resident State throws
                 * rather than dereferencing non-host-addressable memory. @see Buffer::operator[].
                 *
                 * @param i Element index into the current field.
                 * @return A reference to element @p i of the current field.
                 * @throws SushiException If the State is device-resident.
                 */
                T& operator[](std::size_t i) const { return cur_[i]; }

                /**
                 * @brief A host-addressable view over the current field (shared only).
                 *
                 * The branch-free bulk seam for host access to the current state, mirroring
                 * Buffer::host(): one residency check, then unchecked indexing.
                 *
                 * @return A span over the current field, valid while this State lives.
                 * @throws SushiException If the State is device-resident.
                 */
                span<T> host() const { return cur_.host(); }

                /**
                 * @brief Reports where both fields live.
                 * @return Residency::Shared for managed USM, Residency::Device for
                 *         device-resident memory, Residency::Host for pinned host USM.
                 */
                Residency residency() const noexcept { return cur_.residency(); }

                /**
                 * @brief Tells whether the host may index the current field directly.
                 * @return True for shared or host USM; false for device-resident memory, which
                 *         must go through read_range()/write_range().
                 */
                bool host_accessible() const noexcept { return cur_.host_accessible(); }

                /**
                 * @brief Copies a sub-range of the current field out to the host.
                 *
                 * The selective-readback seam for a device-resident field: pull a
                 * window of the current state for telemetry or IO without host-mapping
                 * the whole field. @see Buffer::read_range.
                 *
                 * @param range Element window to read; count is clamped to the field size.
                 * @return A host vector holding the copied elements.
                 */
                std::vector<T> read_range(ElementRange range) const
                {
                    return cur_.read_range(range);
                }

                /**
                 * @brief Seeds a sub-range of the current field from the host.
                 *
                 * The way to initialise a device-resident State the host cannot index.
                 * @see Buffer::write_range.
                 *
                 * @param range Element window to write; count is clamped to the field size.
                 * @param src   Host source of at least range.count elements.
                 */
                void write_range(ElementRange range, const T* src)
                {
                    cur_.write_range(range, src);
                }

                /**
                 * @brief Swaps current and next so the just-written state becomes
                 *        current. The runtime calls this once per step.
                 */
                void advance() noexcept
                {
                    using std::swap;
                    swap(cur_, next_);
                }

#ifdef SR_DISTRIBUTED
                /**
                 * @brief Registers both fields as distributable buffers (Master mode).
                 *
                 * Opt-in, mirroring Buffer::distributed(): exposes each of the State's
                 * two allocations to the cluster so a stepped graph can offload its
                 * update rule. The two allocations keep stable handles for the State's
                 * lifetime; only which one is read versus written rotates each step,
                 * and the offload path follows that from the live pointers. No-op when
                 * empty.
                 *
                 * @return *this, for chaining.
                 */
                State& distributed() &
                {
                    cur_.distributed();
                    next_.distributed();
                    return *this;
                }

                /** @brief Rvalue overload; moves the state into the caller. */
                State distributed() &&
                {
                    cur_.distributed();
                    next_.distributed();
                    return std::move(*this);
                }
#endif // SR_DISTRIBUTED

            private:
                Buffer<T> cur_;
                Buffer<T> next_;
        };

    } // namespace API

    using API::State;

} // namespace SushiRuntime
