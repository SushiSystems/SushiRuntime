/****************************************************************/
/* ref_counted.hpp                                              */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

#include <atomic>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/logger.hpp>

namespace SushiRuntime 
{
    namespace Core
    {
        /**
         * @brief Intrusive reference-counted base for objects managed by sushi_ptr.
         *
         * The counter starts at 0; each sushi_ptr copy increments it with relaxed
         * ordering (no synchronisation needed — ownership cannot change without an
         * existing reference). The last release() issues a release fence before
         * calling delete, ensuring all prior writes are visible to any thread that
         * had access through an earlier reference.
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
                 * @brief Acquires a reference. Called by sushi_ptr on copy construction/assignment.
                 *
                 * Relaxed ordering is intentional: add_ref() is only called by a
                 * thread that already holds a reference, so no additional synchronisation
                 * is needed to observe the object's state.
                 */
                void add_ref() const noexcept
                {
                    ref_count.fetch_add(1, std::memory_order_relaxed);
                }

                /**
                 * @brief Releases a reference, deleting the object when the count reaches zero.
                 *
                 * The fetch_sub uses release ordering so all prior writes are visible to
                 * the thread that observes the count drop to zero; the acquire fence before
                 * delete pairs with that release to prevent the deletion from being reordered
                 * before the final reference drop.
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