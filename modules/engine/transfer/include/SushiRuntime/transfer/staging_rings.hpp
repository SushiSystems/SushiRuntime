/**************************************************************************/
/* staging_rings.hpp                                                      */
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
 * @file staging_rings.hpp
 * @brief Declares StagingRings, one staging ring per SYCL context, each pinned in its context.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/transfer/pinned_host_memory.hpp>
#include <SushiRuntime/transfer/staging_ring.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Holds one StagingRing per SYCL context, so each context stages through memory
         *        pinned in that context.
         *
         * Rings are added between runs; ring_of() then only reads, and takes no lock.
         */
        class StagingRings final
        {
            public:
                /** @param shape Size of every ring; a zero field is raised to 1. */
                explicit StagingRings(StagingRingShape shape = {}) noexcept;

                StagingRings(const StagingRings&) = delete;
                StagingRings& operator=(const StagingRings&) = delete;

                /**
                 * @brief Adds a ring for context whose slots memory makes; a context with a ring keeps it.
                 * @param memory Makes pinned memory in context; must outlive this object.
                 */
                void add(const sycl::context& context, PinnedHostMemory& memory);

                /** @brief Returns the ring of context, or null when none was added. */
                [[nodiscard]] StagingRing* ring_of(const sycl::context& context) const noexcept;

                /** @brief Returns the size of every ring. */
                [[nodiscard]] StagingRingShape shape() const noexcept;

            private:
                /// @brief Size of every ring.
                StagingRingShape shape_;

                /// @brief Context of each ring, in the order they were added.
                std::vector<sycl::context> contexts_;

                /// @brief Ring of each context in contexts_.
                std::vector<std::unique_ptr<StagingRing>> rings_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
