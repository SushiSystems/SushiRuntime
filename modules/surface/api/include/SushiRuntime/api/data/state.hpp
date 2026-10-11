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
        template <typename T>
        class State;

        namespace detail
        {
            /**
             * @brief Holds what a recorded rule reads of a State: its two fields and their size.
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            template <typename T>
            struct StateView
            {
                T* current = nullptr;       /**< Base of the field a step reads. */
                T* next = nullptr;          /**< Base of the field a step writes. */
                std::size_t size = 0;       /**< Elements in each field. */
                State<T>* owner = nullptr;  /**< The State that writes this view, or null. */
            };
        } // namespace detail

        /**
         * @brief Owns two equally sized buffers whose roles the runtime swaps after each step.
         * @tparam T Element type; must be trivially copyable, which its Buffer<T> members check
         *         at compile time.
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
                 * @brief Takes other's fields and the view graphs recorded, leaving other empty.
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                State(State&& other) noexcept
                    : current_(std::move(other.current_)),
                      next_(std::move(other.next_)),
                      view_(std::move(other.view_))
                {
                    adopt_view();
                }

                /**
                 * @brief Frees this state's fields, then takes other's fields and view.
                 * @param other State to take from; left empty.
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                State& operator=(State&& other) noexcept
                {
                    if (this != &other)
                    {
                        retire_view();
                        current_ = std::move(other.current_);
                        next_ = std::move(other.next_);
                        view_ = std::move(other.view_);
                        adopt_view();
                    }
                    return *this;
                }

                State(const State&) = delete;
                State& operator=(const State&) = delete;

                /** @brief Frees both fields and empties the view graphs recorded. */
                ~State()
                {
                    retire_view();
                }

                /**
                 * @brief Exposes the field a step kernel reads from.
                 * @return Pointer to the first element of the current field.
                 */
                [[nodiscard]] T* current() const noexcept { return current_.data(); }

                /**
                 * @brief Exposes the field a step kernel writes into.
                 * @return Pointer to the first element of the next field.
                 */
                [[nodiscard]] T* next() const noexcept { return next_.data(); }

                /**
                 * @brief Reports the capacity of the pair.
                 * @return Number of elements in each field; both are the same size.
                 */
                [[nodiscard]] std::size_t size() const noexcept { return current_.size(); }

                /**
                 * @brief Tells whether this State owns no memory.
                 * @return True if it owns no allocation, e.g. after being moved from.
                 */
                [[nodiscard]] bool empty() const noexcept { return current_.empty(); }

                /**
                 * @brief Reports which device queue both fields are pinned to.
                 * @return The device index the State was allocated on.
                 */
                [[nodiscard]] DeviceIndex device_index() const noexcept
                {
                    return current_.device_index();
                }

                /**
                 * @brief Reports where both fields live.
                 * @return Residency::Shared for managed USM, Residency::Device for
                 *         device-resident memory, Residency::Host for pinned host USM.
                 */
                [[nodiscard]] Residency residency() const noexcept { return current_.residency(); }

                /**
                 * @brief Tells whether the host may index the current field directly.
                 * @return True for shared or host USM; false for device-resident memory, which
                 *         must go through read_range()/write_range().
                 */
                [[nodiscard]] bool is_host_accessible() const noexcept
                {
                    return current_.is_host_accessible();
                }

                /**
                 * @brief Returns element i of the current field for host code.
                 * @pre i < size().
                 * @throws SushiException If the State is device-resident.
                 */
                T& operator[](std::size_t i) const
                {
                    current_.require_host_accessible("State::operator[]");
                    return current_.data()[i];
                }

                /**
                 * @brief Returns a host-addressable span over the current field after one residency
                 *        check.
                 * @throws SushiException If the State is device-resident.
                 */
                [[nodiscard]] span<T> host() const
                {
                    current_.require_host_accessible("State::host");
                    return span<T>(current_.data(), current_.size());
                }

                /**
                 * @brief Copies a window of the current field out to a host vector and waits for
                 *        it.
                 * @param range Element window to read.
                 * @throws SushiException With invalid_access when @p range does not fit size().
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                [[nodiscard]] std::vector<T> read_range(ElementRange range) const
                {
                    return current_.read_window("State::read_range", range);
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
                    current_.write_window("State::write_range", range, source);
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
                    return current_.region_of("State::region", range);
                }

                /**
                 * @brief Shares both fields with the runtime's offload coordinator; does nothing
                 *        when none is installed or the state is empty.
                 * @return *this.
                 * @throws SushiException With invalid_access when the state owns memory and is
                 *         device-resident.
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                State& distributed() &
                {
                    current_.share_with_offload("State::distributed()");
                    next_.share_with_offload("State::distributed()");
                    return *this;
                }

                /**
                 * @brief Shares both fields as distributed() does, then returns the state by value.
                 * @return The state, moved out of this object.
                 * @throws SushiException With invalid_access when the state owns memory and is
                 *         device-resident.
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                [[nodiscard]] State distributed() &&
                {
                    current_.share_with_offload("State::distributed()");
                    next_.share_with_offload("State::distributed()");
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
                    current_.write_window("State::write_range", range,
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
                 * @param call Names the public operation in a thrown message.
                 * @throws SushiException With invalid_access or allocation_failed, as Buffer's
                 *         allocating constructor does.
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                State(const char* call, std::shared_ptr<Execution::RuntimeContext> ctx,
                      std::size_t count, DeviceIndex device_index, Residency residency)
                    : current_(call, ctx, count, device_index, residency),
                      next_(call, std::move(ctx), count, device_index, residency),
                      view_(std::make_shared<detail::StateView<T>>())
                {
                    publish_view();
                }

                /**
                 * @brief Writes both field pointers, the size and this address into the view.
                 * @pre view_ is not null.
                 */
                void publish_view() noexcept
                {
                    view_->current = current_.data();
                    view_->next = next_.data();
                    view_->size = current_.size();
                    view_->owner = this;
                }

                /** @brief Names this object as the owner of the view it holds, if any. */
                void adopt_view() noexcept
                {
                    if (view_)
                        view_->owner = this;
                }

                /** @brief Empties the view it holds, if any, and lets go of it. */
                void retire_view() noexcept
                {
                    if (view_)
                        *view_ = detail::StateView<T>();
                    view_.reset();
                }

                /** @brief The field a step reads. */
                Buffer<T> current_;

                /** @brief The field a step writes. */
                Buffer<T> next_;

                /** @brief Shared with every graph that recorded this State; null when empty. */
                std::shared_ptr<detail::StateView<T>> view_;
        };

    } // namespace API

    using API::State;

} // namespace SushiRuntime
