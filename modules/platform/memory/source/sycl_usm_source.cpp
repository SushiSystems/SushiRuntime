/**************************************************************************/
/* sycl_usm_source.cpp                                                    */
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
 * @file sycl_usm_source.cpp
 * @brief Defines SYCLUSMSource, which takes USM from a set of SYCL device queues.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#include <SushiRuntime/memory/sycl_usm_source.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/core/assert.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        namespace
        {
            /** @brief Makes the unaligned SYCL allocation of a kind; null when SYCL has none. */
            void* allocate_unaligned(USMKind kind, std::size_t bytes, const sycl::queue& queue)
            {
                switch (kind)
                {
                    case USMKind::Host:
                        return sycl::malloc_host(bytes, queue);
                    case USMKind::Shared:
                        return sycl::malloc_shared(bytes, queue);
                    case USMKind::Device:
                        return sycl::malloc_device(bytes, queue);
                }
                return nullptr;
            }

            /** @brief Makes the aligned SYCL allocation of a kind; null when SYCL has none. */
            void* allocate_aligned(USMKind kind, std::size_t alignment, std::size_t bytes,
                                   const sycl::queue& queue)
            {
                switch (kind)
                {
                    case USMKind::Host:
                        return sycl::aligned_alloc_host(alignment, bytes, queue);
                    case USMKind::Shared:
                        return sycl::aligned_alloc_shared(alignment, bytes, queue);
                    case USMKind::Device:
                        return sycl::aligned_alloc_device(alignment, bytes, queue);
                }
                return nullptr;
            }
        } // namespace

        SYCLUSMSource::SYCLUSMSource(std::vector<sycl::queue> device_queues)
            : queues_(std::move(device_queues))
        {

        }

        std::size_t SYCLUSMSource::device_count() const noexcept
        {
            return queues_.size();
        }

        void* SYCLUSMSource::allocate(const USMRequest& request)
        {
            const sycl::queue& queue = queue_at(request.device_index);
            return request.alignment == 0
                ? allocate_unaligned(request.kind, request.bytes, queue)
                : allocate_aligned(request.kind, request.alignment, request.bytes, queue);
        }

        void SYCLUSMSource::free(void* address, std::size_t device_index)
        {
            sycl::free(address, queue_at(device_index));
        }

        std::optional<std::size_t> SYCLUSMSource::resolve_owner(const void* address) const
        {
            std::optional<std::size_t> best;
            for (std::size_t index = 0; index < queues_.size(); ++index)
            {
                const sycl::queue& queue = queues_[index];
                try
                {
                    const sycl::context context = queue.get_context();
                    const sycl::usm::alloc kind = sycl::get_pointer_type(address, context);
                    if (kind == sycl::usm::alloc::unknown || kind == sycl::usm::alloc::host)
                        continue;
                    if (sycl::get_pointer_device(address, context) != queue.get_device())
                        continue;
                    // A shared match wins at once; a device match waits for a later shared one.
                    if (kind == sycl::usm::alloc::shared)
                        return index;
                    best = index;
                }
                catch (const sycl::exception&)
                {
                    // This context cannot resolve the address, which answers "not this device".
                    continue;
                }
            }
            return best;
        }

        bool SYCLUSMSource::shares_context(std::size_t first, std::size_t second) const
        {
            return queue_at(first).get_context() == queue_at(second).get_context();
        }

        std::string SYCLUSMSource::device_name(std::size_t index) const
        {
            return queue_at(index).get_device().get_info<sycl::info::device::name>();
        }

        const sycl::queue& SYCLUSMSource::queue_at(std::size_t device_index) const
        {
            SR_VERIFY(device_index < queues_.size(),
                      ("USM device index " + std::to_string(device_index) +
                       " is out of range: the allocator holds " +
                       std::to_string(queues_.size()) + " device(s).")
                          .c_str());
            return queues_[device_index];
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
