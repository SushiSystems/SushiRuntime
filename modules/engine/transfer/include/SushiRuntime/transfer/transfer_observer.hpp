/**************************************************************************/
/* transfer_observer.hpp                                                  */
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
 * @file transfer_observer.hpp
 * @brief Declares TransferObserver and TransferRecord, the report of which path each copy took.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <string_view>

#include <SushiRuntime/handle/memory_space_id.hpp>
#include <SushiRuntime/handle/transfer_path_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds one copy a path started: the path that took it, the spaces and the length. */
        struct TransferRecord
        {
            std::string_view path;     /**< Name of the path, as TransferPath::name() returns it, for logs. */
            MemorySpaceID source;      /**< Space the bytes were read from. */
            MemorySpaceID destination; /**< Space the bytes were written to. */
            std::size_t bytes = 0;     /**< Bytes copied. */
            TransferPathID id{};       /**< Identity of the path, as TransferPath::id() returns it. */
        };

        /**
         * @brief Hears of each copy a path starts.
         *
         * Paths call it from the worker that runs the copy node, so an observer is safe to call
         * from several threads at once.
         */
        class TransferObserver
        {
            public:
                virtual ~TransferObserver() = default;

                /** @brief Called once per copy, after the path has submitted it. */
                virtual void copied(const TransferRecord& record) = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
