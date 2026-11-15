/**************************************************************************/
/* staging_ring.hpp                                                       */
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
 * @file staging_ring.hpp
 * @brief Declares StagingRing, the fixed ring of pinned host buffers that staged copies pass through.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/transfer/pinned_host_memory.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Bytes of one staging slot unless the owner of the ring says otherwise. */
        inline constexpr std::size_t K_DEFAULT_STAGING_SLOT_BYTES = std::size_t{4} * 1024 * 1024;

        /** @brief Slots of a staging ring unless the owner of the ring says otherwise. */
        inline constexpr std::size_t K_DEFAULT_STAGING_SLOT_COUNT = 4;

        /** @brief Holds the size of a staging ring. */
        struct StagingRingShape
        {
            std::size_t slot_bytes = K_DEFAULT_STAGING_SLOT_BYTES; /**< Bytes of one slot; at least 1. */
            std::size_t slot_count = K_DEFAULT_STAGING_SLOT_COUNT; /**< Slots in the ring; at least 1. */
        };

        /** @brief Names one slot a caller holds. */
        struct StagingSlot
        {
            std::size_t index = 0;   /**< Position of the slot in the ring. */
            void* data = nullptr;    /**< First byte of the slot, pinned host memory. */
            sycl::event previous_use; /**< Last use of the slot by its previous holder; a write into it depends on it. */
        };

        /**
         * @brief A fixed set of pinned host buffers, handed out one at a time and reused.
         *
         * The slots are made once, on the first acquire(), and freed with the ring, so a staged
         * copy never allocates. A slot has one holder at a time. When the holder retires it with
         * the event of its last read, the next holder gets that event and orders its write after
         * it. The ring locks only to hand a slot out or take it back, never while bytes move.
         */
        class StagingRing
        {
            public:
                /**
                 * @param memory Makes the slots; must outlive the ring.
                 * @param shape Size of the ring; a zero field is raised to 1.
                 */
                explicit StagingRing(PinnedHostMemory& memory, StagingRingShape shape = {}) noexcept;

                StagingRing(const StagingRing&) = delete;
                StagingRing& operator=(const StagingRing&) = delete;

                /** @brief Waits for the last use of every slot, then frees the slots. */
                ~StagingRing();

                /** @brief Returns the size of the ring. */
                [[nodiscard]] StagingRingShape shape() const noexcept;

                /**
                 * @brief Takes the next free slot, in ring order, with the event of its last use.
                 *
                 * Never waits for a device; blocks only while every slot is held by a caller.
                 * @return The slot; AllocationFailed when the pinned memory has no room for the slots.
                 */
                [[nodiscard]] Result<StagingSlot> acquire();

                /**
                 * @brief Gives a slot back.
                 * @param slot A slot acquire() returned and the caller still holds.
                 * @param last_use The event after which nothing reads the slot; an empty event for none.
                 */
                void retire(const StagingSlot& slot, const sycl::event& last_use);

            private:
                /** @brief Makes every slot on the first call. Must run under mutex_. */
                [[nodiscard]] Result<void> allocate_slots();

                /// @brief Makes and frees the slots.
                PinnedHostMemory* memory_;

                /// @brief Size of the ring.
                StagingRingShape shape_;

                /// @brief Guards the fields below.
                std::mutex mutex_;

                /// @brief Wakes a caller that waits for a free slot.
                std::condition_variable freed_;

                /// @brief First byte of each slot; empty until the first acquire().
                std::vector<void*> slots_;

                /// @brief Event of the last use of each slot.
                std::vector<sycl::event> last_use_;

                /// @brief Whether a caller holds each slot.
                std::vector<bool> held_;

                /// @brief Slot the next acquire() tries first.
                std::size_t cursor_ = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
