/**************************************************************************/
/* buffer_registry.cpp                                                    */
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

#include <SushiRuntime/distributed/buffer_registry.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        BufferHandle BufferRegistry::register_buffer(void* usm_ptr, uint64_t size_bytes)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (auto it = by_ptr_.find(usm_ptr); it != by_ptr_.end())
            {
                // Re-registration of a known address. This is also how a fresh USM
                // allocation that reused a freed address shows up (the registry is
                // not told when USM is freed). Bump the version so any worker still
                // caching the old buffer under this handle re-pulls the current
                // bytes instead of serving stale data; the id (and its id->ptr
                // mapping) stays valid.
                ++it->second.version;
                it->second.size_bytes = size_bytes;
                return it->second;
            }

            BufferHandle h;
            h.id.value    = next_id_++;
            h.version     = 1;
            h.size_bytes  = size_bytes;

            by_ptr_[usm_ptr]      = h;
            ptr_by_id_[h.id.value] = usm_ptr;
            return h;
        }

        void BufferRegistry::bump_version(void* usm_ptr)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = by_ptr_.find(usm_ptr); it != by_ptr_.end())
                ++it->second.version;
        }

        std::optional<BufferHandle> BufferRegistry::lookup(void* usm_ptr) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = by_ptr_.find(usm_ptr); it != by_ptr_.end())
                return it->second;
            return std::nullopt;
        }

        std::optional<BufferRegistry::Entry> BufferRegistry::lookup_by_id(GlobalBufferID id) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto pit = ptr_by_id_.find(id.value);
            if (pit == ptr_by_id_.end())
                return std::nullopt;

            auto hit = by_ptr_.find(pit->second);
            if (hit == by_ptr_.end())
                return std::nullopt;

            return Entry{ pit->second, hit->second };
        }

    } // namespace Distributed
} // namespace SushiRuntime
