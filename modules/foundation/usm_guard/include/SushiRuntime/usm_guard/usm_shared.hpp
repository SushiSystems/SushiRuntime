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
 * @file usm_shared.hpp
 * @brief Declares the move-only guard that frees a shared USM allocation.
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>

#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Move-only RAII guard for a sycl::malloc_shared buffer.
         *
         * Ensures that the allocated USM memory is automatically freed upon exit.
         */
        template <typename T>
        class USMShared
        {
        public:
            /**
             * @brief Allocates shared USM memory.
             * @param q The SYCL queue used for allocation.
             * @param count Number of elements of type T to allocate.
             */
            USMShared(sycl::queue& q, size_t count)
                : queue_(&q), ptr_(sycl::malloc_shared<T>(count, q)), count_(count) {}

            ~USMShared() { reset(); }

            USMShared(const USMShared&) = delete;
            USMShared& operator=(const USMShared&) = delete;

            USMShared(USMShared&& other) noexcept
                : queue_(other.queue_), ptr_(other.ptr_), count_(other.count_)
            {
                other.ptr_ = nullptr;
            }

            USMShared& operator=(USMShared&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    queue_ = other.queue_;
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
            void reset()
            {
                if (ptr_ && queue_)
                    sycl::free(ptr_, *queue_);
                ptr_ = nullptr;
            }

            sycl::queue* queue_ = nullptr;
            T* ptr_ = nullptr;
            size_t count_ = 0;
        };
    } // namespace Test
} // namespace SushiRuntime
