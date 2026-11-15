/**************************************************************************/
/* host_write_watch.cpp                                                   */
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
 * @file host_write_watch.cpp
 * @brief Implements HostWriteWatch.
 * @author Mustafa Garip
 */

#include "host_write_watch.hpp"

#include <algorithm>

namespace SushiRuntime
{
    namespace Detail
    {
        HostWriteWatch::HostWriteWatch(const std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                                       const Internal::Memory::ResidencyDirectory& directory)
            : armed_(directory.host_write_generation())
        {
            for (const sushi_ptr<Internal::Graph::Node>& node : nodes)
            {
                for (const BufferRegion& region : node->io_reads_)
                    handles_.push_back(region.base);
                for (const BufferRegion& region : node->io_writes_)
                    handles_.push_back(region.base);
            }
            std::sort(handles_.begin(), handles_.end());
            handles_.erase(std::unique(handles_.begin(), handles_.end()), handles_.end());
        }

        bool HostWriteWatch::written(const Internal::Memory::ResidencyDirectory& directory) noexcept
        {
            const std::uint64_t current = directory.host_write_generation();
            if (current == armed_)
                return false;
            for (GlobalHandle handle : handles_)
                if (directory.host_write_of(handle) > armed_)
                    return true;
            armed_ = current;
            return false;
        }
    } // namespace Detail
} // namespace SushiRuntime
