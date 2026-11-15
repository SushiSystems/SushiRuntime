/**************************************************************************/
/* runtime_transfer_paths.hpp                                             */
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
 * @file runtime_transfer_paths.hpp
 * @brief Declares RuntimeTransferPaths, the transfer paths the devices of a runtime support.
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <vector>

#include <SushiRuntime/coherence/file_path.hpp>
#include <SushiRuntime/coherence/transfer_observer.hpp>
#include <SushiRuntime/coherence/transfer_path.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Owns the transfer paths of one runtime and the objects they stand on.
         *
         * A path lists first the cheapest way: the copy inside one context, then the peer copy, then the copy staged through pinned host memory, then the read out of a file.
         */
        struct RuntimeTransferPaths
        {
            /** @brief Owns the objects a path holds a reference to; held behind a pointer so it never moves. */
            struct Support;

            RuntimeTransferPaths();
            RuntimeTransferPaths(RuntimeTransferPaths&&) noexcept;
            RuntimeTransferPaths& operator=(RuntimeTransferPaths&&) noexcept;
            ~RuntimeTransferPaths();

            std::unique_ptr<Support> support;                          /**< The queues and the access the paths use. */
            std::vector<std::unique_ptr<Coherence::TransferPath>> owned; /**< The paths. */
            std::vector<Coherence::TransferPath*> views;               /**< The same paths, in the order they are tried. */
            Coherence::FilePath* file = nullptr;                       /**< The path that reads file spaces; owned holds it. */
        };

        /**
         * @brief Builds the paths that copy between the devices of devices.
         * @param devices Names the queue of each device; must outlive the result.
         * @param observer Hears each copy; null for none; must outlive the result.
         */
        [[nodiscard]] RuntimeTransferPaths build_transfer_paths(Device::DeviceQueueSet& devices,
                                                                Coherence::TransferObserver* observer = nullptr);
    } // namespace Internal::Execution
} // namespace SushiRuntime
