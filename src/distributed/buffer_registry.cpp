/**************************************************************************/
/* buffer_registry.cpp                                                    */
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

#include <SushiRuntime/distributed/buffer_registry.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        BufferHandle BufferRegistry::register_buffer(void* usm_ptr, uint64_t size_bytes)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (auto it = by_ptr_.find(usm_ptr); it != by_ptr_.end())
                return it->second;

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
