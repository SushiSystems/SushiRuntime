/**************************************************************************/
/* heap_cache_allocator.hpp                                               */
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
 * @file heap_cache_allocator.hpp
 * @brief Declares HeapCacheAllocator, the heap adapter of the worker cache's allocator port.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <map>
#include <memory>

#include <SushiRuntime/dispatch_executor/worker_buffer_cache.hpp>

namespace TestSupport
{
    /**
     * @brief Serves the blocks of a WorkerBufferCache from the heap and counts the live ones.
     *
     * The adapter of the CacheAllocator port a test builds a cache on; it fails an allocation
     * past its limit, as a device with no room does.
     */
    class HeapCacheAllocator final
    {
        public:
            /**
             * @brief Builds an allocator that serves blocks up to a total.
             * @param limit_bytes Most bytes live at once; an allocation past it returns null.
             */
            explicit HeapCacheAllocator(std::size_t limit_bytes = static_cast<std::size_t>(-1))
                : limit_bytes_(limit_bytes)
            {

            }

            /** @brief Returns the port: allocate and release bound to this object. */
            [[nodiscard]] SushiRuntime::Distributed::CacheAllocator port()
            {
                SushiRuntime::Distributed::CacheAllocator allocator;
                allocator.allocate = [this](std::size_t size_bytes) -> void*
                {
                    return take(size_bytes);
                };
                allocator.release = [this](void* address)
                {
                    blocks_.erase(address);
                };
                return allocator;
            }

            /** @brief Returns the number of blocks not released yet. */
            [[nodiscard]] std::size_t live_blocks() const noexcept
            {
                return blocks_.size();
            }

        private:
            /** @brief Holds one block and its size. */
            struct Block
            {
                std::unique_ptr<std::byte[]> bytes; /**< The block's memory. */
                std::size_t size_bytes = 0;         /**< The block's size. */
            };

            /** @brief Returns a zeroed block, or null when it would pass the limit. */
            [[nodiscard]] void* take(std::size_t size_bytes)
            {
                if (live_bytes() + size_bytes > limit_bytes_)
                    return nullptr;
                Block block;
                block.bytes.reset(new std::byte[size_bytes]());
                block.size_bytes = size_bytes;
                void* address = block.bytes.get();
                blocks_[address] = std::move(block);
                return address;
            }

            /** @brief Returns the bytes of the blocks not released yet. */
            [[nodiscard]] std::size_t live_bytes() const
            {
                std::size_t total = 0;
                for (const auto& block : blocks_)
                    total += block.second.size_bytes;
                return total;
            }

            /// @brief Most bytes live at once.
            std::size_t limit_bytes_;

            /// @brief The blocks handed out and not released, by address.
            std::map<void*, Block> blocks_;
    };
} // namespace TestSupport
