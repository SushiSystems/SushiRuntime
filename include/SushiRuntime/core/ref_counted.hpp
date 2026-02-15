/**************************************************************************/
/* ref_counted.hpp                                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#pragma once

#include <atomic>
#include <iostream>
#include "logger.hpp"

namespace SushiRuntime 
{
    namespace Core
    {
        /**
         * @brief base class for reference counting.
         * 
         * this class implements an intrusive reference counting mechanism.
         * it manages the lifetime of objects using atomic operations.
         */
        class RefCounted 
        {
            protected:
                /** @brief atomic reference counter. */
                mutable std::atomic<int32_t> ref_count{0};

                /** @brief virtual destructor for safe inheritance. */
                virtual ~RefCounted() = default;

            public:
                /**
                 * @brief increases the reference count by 1.
                 */
                void add_ref() const noexcept 
                {
                    int32_t old = ref_count.fetch_add(1, std::memory_order_relaxed);
                    SR_LOG_DEBUG("ref add: {} -> {}", old, old + 1);
                }

                /**
                 * @brief decreases the reference count by 1. deletes the object if count reaches zero.
                 */
                void release() const noexcept 
                {
                    int32_t old = ref_count.fetch_sub(1, std::memory_order_release);
                    SR_LOG_DEBUG("ref release: {} -> {}", old, old - 1);

                    if (old == 1) 
                    {
                        std::atomic_thread_fence(std::memory_order_acquire);
                        delete this; 
                    }
                }

                /**
                 * @brief returns the current reference count.
                 * @return int32_t the reference count value.
                 */
                int32_t get_ref_count() const noexcept 
                {
                    return ref_count.load(std::memory_order_relaxed);
                }
        };
        
    } // namespace Core
} // namespace SushiRuntime