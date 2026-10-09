/**************************************************************************/
/* state.hpp                                                              */
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
 * @file state.hpp
 * @brief Declares State, a field the runtime double-buffers across steps.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/data/README.md
 */

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
             * @brief Holds the current and next pointers a stepped kernel reads afresh each step.
             * @see include/SushiRuntime/api/data/README.md
             */
            template <typename T>
            struct StateView
            {
                T* cur = nullptr;   /**< Current-state base pointer for this step. */
                T* next = nullptr;  /**< Next-state base pointer for this step. */
            };
        } // namespace detail

        /**
         * @brief Owns two equally sized buffers whose roles the runtime swaps after each step.
         * @tparam T Element type; must be trivially copyable for device use.
         * @see include/SushiRuntime/api/data/README.md
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
                 * @brief Allocates both buffers of count elements on one device.
                 * @see include/SushiRuntime/api/data/README.md
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
                 * @brief Returns a weak observer of the current field's allocation, which stands
                 *        for the State.
                 * @see include/SushiRuntime/api/data/README.md
                 */
                Core::LivenessObserver liveness() const noexcept { return cur_.liveness(); }

                /**
                 * @brief Returns element i of the current field for host code.
                 * @throws SushiException If the State is device-resident.
                 */
                T& operator[](std::size_t i) const { return cur_[i]; }

                /**
                 * @brief Returns a host-addressable span over the current field after one residency
                 *        check.
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
                 * @brief Copies a sub-range of the current field out to a host vector.
                 * @param range Element window to read; count is clamped to the field size.
                 */
                std::vector<T> read_range(ElementRange range) const
                {
                    return cur_.read_range(range);
                }

                /**
                 * @brief Copies a host range into a sub-range of the current field.
                 * @param range Element window to write; count is clamped to the field size.
                 * @param src Host source of at least range.count elements.
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

                /**
                 * @brief Shares both fields with the runtime's offload coordinator; does nothing
                 *        when none is installed or the state is empty.
                 * @see include/SushiRuntime/api/data/README.md
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

            private:
                Buffer<T> cur_;
                Buffer<T> next_;
        };

    } // namespace API

    using API::State;

} // namespace SushiRuntime
