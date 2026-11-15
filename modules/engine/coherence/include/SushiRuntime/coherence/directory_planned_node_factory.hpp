/**************************************************************************/
/* directory_planned_node_factory.hpp                                     */
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
 * @file directory_planned_node_factory.hpp
 * @brief Declares DirectoryPlannedNodeFactory, which builds planned nodes over the directory.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <vector>

#include <SushiRuntime/coherence/instance_allocator.hpp>
#include <SushiRuntime/coherence/planned_node_factory.hpp>
#include <SushiRuntime/memory/residency_view.hpp>
#include <SushiRuntime/transfer/transfer_path.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Builds allocate, copy and free nodes that resolve their instances in the
         *        directory when they run.
         *
         * Each node is pinned to the device of the space it makes, writes or frees; a step on the
         * host space runs on device 0. Each node shares the allocator; the directory and the paths
         * must outlive every node built.
         */
        class DirectoryPlannedNodeFactory final : public PlannedNodeFactory
        {
            public:
                /** @brief Builds a factory over the directory, the allocator and the planner's paths. */
                DirectoryPlannedNodeFactory(Memory::ResidencyView& directory,
                                            std::shared_ptr<InstanceAllocator> allocator,
                                            std::vector<TransferPath*> paths);

                [[nodiscard]] Result<sushi_ptr<Graph::Node>> make(const PlannedStep& step) override;

            private:
                /// @brief Directory the nodes resolve and record instances in.
                Memory::ResidencyView* directory_;

                /// @brief Makes and frees instances.
                std::shared_ptr<InstanceAllocator> allocator_;

                /// @brief Paths, in the planner's order.
                std::vector<TransferPath*> paths_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
