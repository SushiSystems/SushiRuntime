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
#include <optional>
#include <vector>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/device_access_gate.hpp>
#include <SushiRuntime/transfer/file_path.hpp>
#include <SushiRuntime/report/transfer_observer.hpp>
#include <SushiRuntime/transfer/transfer_path.hpp>
#include <SushiRuntime/transfer/transfer_path_list.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Owns the transfer paths of one runtime and the objects they stand on.
         *
         * A path lists first the cheapest way: the copy inside one context, then the peer copy, then the copy staged through pinned host memory, then the read out of a file.
         * A path whose id a listed path has is refused when the list is built: it is logged and
         * recorded in refusal.
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
            std::optional<Error> refusal;                              /**< Set when two paths share an id; the runtime is not created. */
        };

        /**
         * @brief Builds the paths that copy between the devices of devices.
         * @param devices Names the queue of each device; must outlive the result.
         * @param observer Hears each copy; null for none; must outlive the result.
         * @param gate Opened around each copy of every path; null for none; must outlive the result.
         */
        [[nodiscard]] RuntimeTransferPaths build_transfer_paths(Device::DeviceQueueSet& devices,
                                                                SushiRuntime::TransferObserver* observer = nullptr,
                                                                Memory::DeviceAccessGate* gate = nullptr);
    } // namespace Internal::Execution
} // namespace SushiRuntime
