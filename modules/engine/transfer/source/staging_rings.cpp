/**************************************************************************/
/* staging_rings.cpp                                                      */
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
 * @file staging_rings.cpp
 * @brief Implements StagingRings.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/transfer/staging_rings.hpp>

#include <algorithm>
#include <cstddef>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        StagingRings::StagingRings(StagingRingShape shape) noexcept : shape_(shape)
        {

        }

        void StagingRings::add(const sycl::context& context, PinnedHostMemory& memory)
        {
            if (ring_of(context) != nullptr)
                return;
            contexts_.push_back(context);
            rings_.push_back(std::make_unique<StagingRing>(memory, shape_));
        }

        StagingRing* StagingRings::ring_of(const sycl::context& context) const noexcept
        {
            for (std::size_t index = 0; index < contexts_.size(); ++index)
                if (contexts_[index] == context)
                    return rings_[index].get();
            return nullptr;
        }

        StagingRingShape StagingRings::shape() const noexcept
        {
            return StagingRingShape{std::max<std::size_t>(shape_.slot_bytes, 1),
                                    std::max<std::size_t>(shape_.slot_count, 1)};
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
