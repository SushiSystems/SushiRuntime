/**************************************************************************/
/* ref_counted.hpp                                                        */
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
 * @file ref_counted.hpp
 * @brief Declares RefCounted, the intrusive reference-count base of sushi_ptr.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Provides the intrusive reference count for objects managed by sushi_ptr.
         * @see modules/foundation/core/README.md
         */
        class SR_TYPE_API RefCounted
        {
            public:
                RefCounted() = default;
                RefCounted(const RefCounted&) = delete;
                RefCounted& operator=(const RefCounted&) = delete;

                /**
                 * @brief Acquires a reference to the object.
                 * @pre The object has not been deleted.
                 */
                void add_ref() const noexcept
                {
                    // Relaxed: the increment publishes no object state; release() orders the delete.
                    ref_count_.fetch_add(1, std::memory_order_relaxed);
                }

                /**
                 * @brief Releases a reference and deletes the object when it was the last one.
                 * @pre The caller owns one reference and the object has not been deleted.
                 */
                void release() const noexcept
                {
                    std::int32_t old = ref_count_.fetch_sub(1, std::memory_order_release);

                    if (old == 1)
                    {
                        // Pairs with the release decrement of every earlier release() call.
                        std::atomic_thread_fence(std::memory_order_acquire);
                        delete this;
                    }
                }

                /** @brief Returns the current reference count. */
                [[nodiscard]] std::int32_t get_ref_count() const noexcept
                {
                    return ref_count_.load(std::memory_order_relaxed);
                }

            protected:
                /** @brief Destroys the object through its most derived type. */
                virtual ~RefCounted() = default;

            private:
                /** @brief Holds the atomic reference count; zero for an unmanaged object. */
                mutable std::atomic<std::int32_t> ref_count_{0};
        };

    } // namespace Core
} // namespace SushiRuntime
