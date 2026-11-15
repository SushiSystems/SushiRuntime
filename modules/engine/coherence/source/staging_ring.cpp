/**************************************************************************/
/* staging_ring.cpp                                                       */
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
 * @file staging_ring.cpp
 * @brief Implements StagingRing.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/staging_ring.hpp>

#include <SushiRuntime/coherence/coherence_log_category.hpp>
#include <SushiRuntime/log/logger.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        StagingRing::StagingRing(PinnedHostMemory& memory, StagingRingShape shape) noexcept
            : memory_(&memory), shape_{std::max<std::size_t>(shape.slot_bytes, 1), std::max<std::size_t>(shape.slot_count, 1)}
        {

        }

        StagingRing::~StagingRing()
        {
            for (std::size_t index = 0; index < slots_.size(); ++index)
            {
                try
                {
                    last_use_[index].wait();
                }
                catch (const sycl::exception& error)
                {
                    SR_LOG_WARN(K_COHERENCE_LOG_CATEGORY,
                                "A staging slot's last copy failed while the ring closed: {}",
                                error.what());
                }
                memory_->release(slots_[index]);
            }
        }

        StagingRingShape StagingRing::shape() const noexcept
        {
            return shape_;
        }

        void StagingRing::allocate_slots()
        {
            std::vector<void*> made;
            made.reserve(shape_.slot_count);
            for (std::size_t index = 0; index < shape_.slot_count; ++index)
            {
                void* const slot = memory_->allocate(shape_.slot_bytes);
                if (slot == nullptr)
                {
                    for (void* const done : made)
                        memory_->release(done);
                    throw std::runtime_error("staging ring: the pinned host memory has no room for the slots.");
                }
                made.push_back(slot);
            }
            slots_ = std::move(made);
            last_use_.assign(shape_.slot_count, sycl::event{});
            held_.assign(shape_.slot_count, false);
        }

        StagingSlot StagingRing::acquire()
        {
            std::size_t index = 0;
            sycl::event previous;
            void* data = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (slots_.empty())
                    allocate_slots();
                const auto free_slot = [this]
                {
                    for (std::size_t step = 0; step < shape_.slot_count; ++step)
                    {
                        const std::size_t candidate = (cursor_ + step) % shape_.slot_count;
                        if (!held_[candidate])
                            return candidate;
                    }
                    return shape_.slot_count;
                };
                index = free_slot();
                while (index == shape_.slot_count)
                {
                    freed_.wait(lock);
                    index = free_slot();
                }
                held_[index] = true;
                cursor_ = (index + 1) % shape_.slot_count;
                previous = last_use_[index];
                data = slots_[index];
            }
            try
            {
                previous.wait();
            }
            catch (...)
            {
                retire(StagingSlot{index, data}, sycl::event{});
                throw;
            }
            return StagingSlot{index, data};
        }

        void StagingRing::retire(const StagingSlot& slot, const sycl::event& last_use)
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                last_use_[slot.index] = last_use;
                held_[slot.index] = false;
            }
            freed_.notify_one();
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
