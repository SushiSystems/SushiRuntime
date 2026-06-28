/**************************************************************************/
/* state.hpp                                                              */
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
                 * @param residency    Shared (host-addressable) or Device (resident).
                 */
                State(std::shared_ptr<Execution::RuntimeContext> ctx,
                      std::size_t count, std::size_t device_index = 0,
                      Residency residency = Residency::Shared)
                    : cur_(ctx, count, device_index, residency),
                      next_(std::move(ctx), count, device_index, residency)
                {
                }

                /** @brief Pointer to the current state; the step reads from here. */
                T* current() const noexcept { return cur_.data(); }

                /** @brief Pointer to the next state; the step writes here. */
                T* next() const noexcept { return next_.data(); }

                /** @brief Number of elements in each buffer. */
                std::size_t size() const noexcept { return cur_.size(); }

                /** @brief True if the state owns no allocation. */
                bool empty() const noexcept { return cur_.empty(); }

                /** @brief Read dependency key (the current buffer's base address). */
                void* read_key() const noexcept { return cur_.dependency_key(); }

                /** @brief Write dependency key (the next buffer's base address). */
                void* write_key() const noexcept { return next_.dependency_key(); }

                /** @brief Host-side access to the current state's elements (shared only). */
                T& operator[](std::size_t i) const noexcept { return cur_[i]; }

                /** @brief Where both fields live (shared or device-resident). */
                Residency residency() const noexcept { return cur_.residency(); }

                /** @brief True if the host may index the current field directly. */
                bool host_accessible() const noexcept { return cur_.host_accessible(); }

                /**
                 * @brief Copies a sub-range of the current field out to the host.
                 *
                 * The selective-readback seam for a device-resident field: pull a
                 * window of the current state for telemetry or IO without host-mapping
                 * the whole field. @see Buffer::read_range.
                 *
                 * @param offset First element to read.
                 * @param count  Number of elements; clamped to the field size.
                 * @return A host vector holding the copied elements.
                 */
                std::vector<T> read_range(std::size_t offset, std::size_t count) const
                {
                    return cur_.read_range(offset, count);
                }

                /**
                 * @brief Seeds a sub-range of the current field from the host.
                 *
                 * The way to initialise a device-resident State the host cannot index.
                 * @see Buffer::write_range.
                 *
                 * @param offset First element to write.
                 * @param src    Host source of at least @p count elements.
                 * @param count  Number of elements; clamped to the field size.
                 */
                void write_range(std::size_t offset, const T* src, std::size_t count)
                {
                    cur_.write_range(offset, src, count);
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
