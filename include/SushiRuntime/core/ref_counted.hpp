/**************************************************************************/
/* ref_counted.hpp                                                        */
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

#include <atomic>
#include <iostream>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/logger.hpp>

namespace SushiRuntime 
{
    namespace Core
    {
        /**
         * @brief Base class for reference counting.
         * 
         * This class implements an intrusive reference counting mechanism.
         * It manages the lifetime of objects using atomic operations.
         */
        class SUSHIRUNTIME_API RefCounted 
        {
            protected:
                /** @brief Atomic reference counter. */
                mutable std::atomic<int32_t> ref_count{0};

                /** @brief Virtual destructor for safe inheritance. */
                virtual ~RefCounted() = default;

            public:

                RefCounted() = default;
                RefCounted(const RefCounted&) = delete;
                RefCounted& operator=(const RefCounted&) = delete;  

                /**
                 * @brief Increases the reference count by 1.
                 */
                void add_ref() const noexcept 
                {
                    ref_count.fetch_add(1, std::memory_order_relaxed);
                }

                /**
                 * @brief Decreases the reference count by 1. Deletes the object if count reaches zero.
                 */
                void release() const noexcept 
                {
                    int32_t old = ref_count.fetch_sub(1, std::memory_order_release);

                    if (old == 1) 
                    {
                        std::atomic_thread_fence(std::memory_order_acquire);
                        delete this; 
                    }
                }

                /**
                 * @brief Returns the current reference count.
                 * @return int32_t The reference count value.
                 */
                int32_t get_ref_count() const noexcept 
                {
                    return ref_count.load(std::memory_order_relaxed);
                }
        };
        
    } // namespace Core
} // namespace SushiRuntime