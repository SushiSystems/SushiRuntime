/**************************************************************************/
/* usm_shared.hpp                                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file usm_shared.hpp
 * @brief Declares the move-only guard that frees a shared USM allocation.
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <memory>
#include <sycl/sycl.hpp>

#include <SushiRuntime/usm_guard/queue_shared_allocation_source.hpp>
#include <SushiRuntime/usm_guard/shared_allocation_source.hpp>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Move-only RAII guard for a shared allocation of count elements of T.
         *
         * Ensures that the allocated memory is freed, through the source it came from, upon exit.
         */
        template <typename T>
        class USMShared
        {
        public:
            /**
             * @brief Allocates shared USM memory on a queue.
             * @param q The SYCL queue used for allocation; must outlive the guard.
             * @param count Number of elements of type T to allocate.
             */
            USMShared(sycl::queue& q, size_t count)
                : owned_(std::make_unique<QueueSharedAllocationSource>(q)), source_(owned_.get()),
                  ptr_(allocate(count)), count_(count) {}

            /**
             * @brief Allocates through a source.
             * @param source Must outlive the guard.
             * @param count Number of elements of type T to allocate.
             */
            USMShared(SharedAllocationSource& source, size_t count)
                : source_(&source), ptr_(allocate(count)), count_(count) {}

            ~USMShared() { reset(); }

            USMShared(const USMShared&) = delete;
            USMShared& operator=(const USMShared&) = delete;

            USMShared(USMShared&& other) noexcept
                : owned_(std::move(other.owned_)), source_(other.source_), ptr_(other.ptr_),
                  count_(other.count_)
            {
                other.ptr_ = nullptr;
            }

            USMShared& operator=(USMShared&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    owned_ = std::move(other.owned_);
                    source_ = other.source_;
                    ptr_   = other.ptr_;
                    count_ = other.count_;
                    other.ptr_ = nullptr;
                }
                return *this;
            }

            T* get() const noexcept                  { return ptr_; }
            T& operator[](size_t i) const            { return ptr_[i]; }
            size_t count() const noexcept            { return count_; }
            explicit operator bool() const noexcept  { return ptr_ != nullptr; }

        private:
            /** @brief Allocates count elements of T through the source. */
            T* allocate(size_t count)
            {
                return static_cast<T*>(source_->allocate(count * sizeof(T), alignof(T)));
            }

            /** @brief Frees the allocation, if one is held, through the source. */
            void reset()
            {
                if (ptr_ && source_)
                    source_->free(ptr_);
                ptr_ = nullptr;
            }

            /// @brief The source a queue guard owns; null for a guard given a source.
            std::unique_ptr<SharedAllocationSource> owned_;
            /// @brief The source the allocation came from.
            SharedAllocationSource* source_ = nullptr;
            /// @brief The allocation, or null.
            T* ptr_ = nullptr;
            /// @brief Number of elements allocated.
            size_t count_ = 0;
        };
    } // namespace Test
} // namespace SushiRuntime
