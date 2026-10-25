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

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/span.hpp>

namespace SushiRuntime
{
    template <typename T>
    class State;

    namespace Detail
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
    } // namespace Detail

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
             * @pre i < size(), and the State is shared or host USM.
             */
            T& operator[](std::size_t i) const
            {
                SR_ASSERT(current_.is_host_accessible(),
                          "State::operator[]: a device-resident state is not host-addressable; "
                          "use read_range() and write_range()");
                return current_.data()[i];
            }

            /**
             * @brief Returns a host-addressable span over the current field after one residency
             *        check.
             * @pre The State is shared or host USM.
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
             * @return The elements, or a KernelFailed error when the device fails the copy.
             * @pre range fits size().
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            [[nodiscard]] Result<std::vector<T>> read_range(ElementRange range) const
            {
                return current_.read_window("State::read_range", range);
            }

            /**
             * @brief Copies host elements into a window of the current field and waits for it.
             * @param range  Element window to write.
             * @param source Holds at least range.count elements; the first range.count are
             *        copied.
             * @return Nothing, or a KernelFailed error when the device fails the copy.
             * @pre range fits size(), and source holds at least range.count elements.
             */
            [[nodiscard]] Result<void> write_range(ElementRange range, span<const T> source) const
            {
                return current_.write_window("State::write_range", range, source);
            }

            /**
             * @brief Names a window of the current field so that tasks on disjoint windows
             *        are not serialized.
             * @param range Element window, in elements.
             * @pre range fits size().
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            [[nodiscard]] BufferRegion region(ElementRange range) const
            {
                return current_.region_of("State::region", range);
            }

        private:
            friend class Runtime;
            friend struct Detail::HandleAccess;

            /**
             * @brief Allocates both fields of count elements on one device.
             * @param call Names the public operation in a message.
             * @return The state, or an AllocationFailed error when the device returns nothing
             *         for a field; a field already allocated is released.
             * @pre device_index names a device, and the byte size of count fits std::size_t.
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            [[nodiscard]] static Result<State> allocate(
                const char* call, const ContextHandle& context,
                std::size_t count, DeviceIndex device_index, Residency residency,
                std::size_t alignment)
            {
                Result<Buffer<T>> current =
                    Buffer<T>::allocate(call, context, count, device_index, residency, alignment);
                if (!current)
                    return make_error(std::move(current.error()));
                Result<Buffer<T>> next =
                    Buffer<T>::allocate(call, context, count, device_index, residency, alignment);
                if (!next)
                    return make_error(std::move(next.error()));

                std::shared_ptr<Detail::StateView<T>> view;
                Result<void> shared = Detail::translate_exceptions([&view]()
                {
                    view = std::make_shared<Detail::StateView<T>>();
                });
                if (!shared)
                    return make_error(std::move(shared.error()));
                return State(std::move(*current), std::move(*next), std::move(view));
            }

            /**
             * @brief Takes the two fields and the view a State is made of.
             * @pre view is not null, and the fields have one size, device and residency.
             */
            State(Buffer<T> current, Buffer<T> next,
                  std::shared_ptr<Detail::StateView<T>> view) noexcept
                : current_(std::move(current)), next_(std::move(next)), view_(std::move(view))
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
                    *view_ = Detail::StateView<T>();
                view_.reset();
            }

            /** @brief The field a step reads. */
            Buffer<T> current_;

            /** @brief The field a step writes. */
            Buffer<T> next_;

            /** @brief Shared with every graph that recorded this State; null when empty. */
            std::shared_ptr<Detail::StateView<T>> view_;
    };



} // namespace SushiRuntime
