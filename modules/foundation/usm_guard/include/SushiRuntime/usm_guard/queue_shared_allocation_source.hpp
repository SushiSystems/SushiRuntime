/**************************************************************************/
/* queue_shared_allocation_source.hpp                                     */
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
 * @file queue_shared_allocation_source.hpp
 * @brief Declares QueueSharedAllocationSource, the SharedAllocationSource of one SYCL queue.
 * @author Mustafa Garip
 * @see modules/foundation/usm_guard/README.md
 */

#pragma once

#include <cstddef>
#include <sycl/sycl.hpp>

#include <SushiRuntime/usm_guard/shared_allocation_source.hpp>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Allocates with sycl::aligned_alloc_shared on a queue and frees on it.
         * @see modules/foundation/usm_guard/README.md
         */
        class QueueSharedAllocationSource final : public SharedAllocationSource
        {
            public:
                /** @param queue Must outlive every allocation of the source. */
                explicit QueueSharedAllocationSource(sycl::queue& queue) noexcept : queue_(&queue) {}

                [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment) override
                {
                    return sycl::aligned_alloc_shared(alignment, bytes, *queue_);
                }

                void free(void* address) noexcept override { sycl::free(address, *queue_); }

            private:
                /// @brief The queue every allocation is made on.
                sycl::queue* queue_;
        };
    } // namespace Test
} // namespace SushiRuntime
