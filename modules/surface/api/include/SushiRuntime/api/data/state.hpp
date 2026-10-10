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
 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>
#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Holds the current and next pointers a stepped kernel reads afresh each step.
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
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
         * @see modules/surface/api/include/SushiRuntime/api/data/README.md
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
                 * @brief Exposes the field a step kernel reads from.
                 * @return Pointer to the first element of the current field.
                 */
                [[nodiscard]] T* current() const noexcept { return cur_.data(); }

                /**
                 * @brief Exposes the field a step kernel writes into.
                 * @return Pointer to the first element of the next field.
                 */
                [[nodiscard]] T* next() const noexcept { return next_.data(); }

                /**
                 * @brief Reports the capacity of the pair.
                 * @return Number of elements in each field; both are the same size.
                 */
                [[nodiscard]] std::size_t size() const noexcept { return cur_.size(); }

                /**
                 * @brief Tells whether this State still owns memory.
                 * @return True if it owns no allocation, e.g. after being moved from.
                 */
                [[nodiscard]] bool empty() const noexcept { return cur_.empty(); }

                /**
                 * @brief Reports which device queue both fields are pinned to.
                 * @return The device index the State was allocated on.
                 */
                [[nodiscard]] DeviceIndex device_index() const noexcept
                {
                    return cur_.device_index();
                }

                /**
                 * @brief Reports where both fields live.
                 * @return Residency::Shared for managed USM, Residency::Device for
                 *         device-resident memory, Residency::Host for pinned host USM.
                 */
                [[nodiscard]] Residency residency() const noexcept { return cur_.residency(); }

                /**
                 * @brief Tells whether the host may index the current field directly.
                 * @return True for shared or host USM; false for device-resident memory, which
                 *         must go through read_range()/write_range().
                 */
                [[nodiscard]] bool is_host_accessible() const noexcept
                {
                    return cur_.is_host_accessible();
                }

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
                [[nodiscard]] span<T> host() const { return cur_.host(); }

                /**
                 * @brief Copies a window of the current field out to a host vector and waits for
                 *        it.
                 * @param range Element window to read.
                 * @throws SushiException With invalid_access when @p range does not fit size().
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                [[nodiscard]] std::vector<T> read_range(ElementRange range) const
                {
                    return cur_.read_window("State::read_range", range);
                }

                /**
                 * @brief Copies host elements into a window of the current field and waits for it.
                 * @param range  Element window to write.
                 * @param source Holds at least range.count elements; the first range.count are
                 *        copied.
                 * @throws SushiException With invalid_access when @p range does not fit size(), or
                 *        @p source is null or shorter than a non-empty window.
                 */
                void write_range(ElementRange range, span<const T> source) const
                {
                    cur_.write_window("State::write_range", range, source);
                }

                /**
                 * @brief Names a window of the current field so that tasks on disjoint windows
                 *        are not serialized.
                 * @param range Element window, in elements.
                 * @throws SushiException With invalid_access when @p range does not fit size().
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                [[nodiscard]] BufferRegion region(ElementRange range) const
                {
                    return cur_.region_of("State::region", range);
                }

                /**
                 * @brief Shares both fields with the runtime's offload coordinator; does nothing
                 *        when none is installed or the state is empty.
                 * @return *this from the lvalue overload, the state by value from the rvalue
                 *        overload.
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                State& distributed() &
                {
                    cur_.distributed();
                    next_.distributed();
                    return *this;
                }

                [[nodiscard]] State distributed() &&
                {
                    cur_.distributed();
                    next_.distributed();
                    return std::move(*this);
                }

                /** @brief Forwards to is_host_accessible(). */
                [[deprecated("use is_host_accessible()")]]
                bool host_accessible() const noexcept
                {
                    return is_host_accessible();
                }

                /**
                 * @brief Forwards to write_range(ElementRange, span<const T>) with range.count
                 *        elements at @p src.
                 * @throws SushiException With invalid_access when @p src is null and range.count
                 *        is not zero.
                 */
                [[deprecated("use write_range(ElementRange, span<const T>)")]]
                void write_range(ElementRange range, const T* src) const
                {
                    cur_.write_window("State::write_range", range,
                                      span<const T>(src, range.count));
                }

                /** @brief Forwards to detail::HandleAccess::read_key(). */
                [[deprecated("name the State in Reads(...) instead")]]
                void* read_key() const noexcept
                {
                    return detail::HandleAccess::read_key(*this);
                }

                /** @brief Forwards to detail::HandleAccess::write_key(). */
                [[deprecated("name the State in Writes(...) instead")]]
                void* write_key() const noexcept
                {
                    return detail::HandleAccess::write_key(*this);
                }

                /** @brief Forwards to detail::HandleAccess::liveness(). */
                [[deprecated("internal to the graph layer; declare the access with Reads or "
                             "Writes")]]
                Memory::LivenessObserver liveness() const noexcept
                {
                    return detail::HandleAccess::liveness(*this);
                }

                /** @brief Forwards to detail::HandleAccess::advance(). */
                [[deprecated("internal to the graph layer; a run advances a State after each "
                             "step")]]
                void advance() noexcept
                {
                    detail::HandleAccess::advance(*this);
                }

            private:
                friend class Runtime;
                friend struct detail::HandleAccess;

                /**
                 * @brief Allocates both fields of count elements on one device.
                 * @throws SushiException With allocation_failed, as Buffer's allocating
                 *         constructor does.
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                State(std::shared_ptr<Execution::RuntimeContext> ctx, std::size_t count,
                      DeviceIndex device_index, Residency residency)
                    : cur_(ctx, count, device_index, residency),
                      next_(std::move(ctx), count, device_index, residency)
                {
                }

                Buffer<T> cur_;
                Buffer<T> next_;
        };

    } // namespace API

    using API::State;

} // namespace SushiRuntime
