/**************************************************************************/
/* split_node_run.hpp                                                     */
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
 * @file split_node_run.hpp
 * @brief Declares the run of a node the partitioner cut into parts.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph { struct Node; }
    namespace Internal::Device { class DeviceQueueSet; }

    namespace Internal::Scheduler
    {
        /**
         * @brief Runs node's parts through dispatch_split(), each on its device's queue, and returns once they ended.
         *
         * The node's dependency events and then the queue of each part's device are waited for.
         * @param node A node whose placed_split is not null.
         * @param devices The queue set the parts' device indices name.
         * @return InvalidArgument for a node with parts and no declared split to run them.
         */
        [[nodiscard]] Result<void> run_split_node(Internal::Graph::Node& node, Internal::Device::DeviceQueueSet& devices);
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
