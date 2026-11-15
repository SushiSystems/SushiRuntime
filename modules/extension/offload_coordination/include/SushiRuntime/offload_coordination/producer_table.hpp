/**************************************************************************/
/* producer_table.hpp                                                     */
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
 * @file producer_table.hpp
 * @brief Declares ProducerTable, the node that wrote the current version of each handle.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/transport.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        struct Node;
    }

    namespace Distributed
    {
        /**
         * @brief Holds who wrote the current version of one handle and where that version is.
         */
        struct ProducerRecord
        {
            /** @brief Holds the version the producer wrote. */
            std::uint32_t version = 0;

            /** @brief Holds the producing node; not owned, null when unknown. */
            Internal::Graph::Node* node = nullptr;

            /** @brief Holds the task id of a remote write; 0 for a local one. */
            std::uint64_t task_id = 0;

            /** @brief Holds the only machine with the whole version; peer 0 is the master. */
            PeerID holder;
        };

        /**
         * @brief Holds one instance a lost worker took with it: the handle and its producer.
         */
        struct LostInstance
        {
            /** @brief Holds the handle whose current version is gone. */
            GlobalHandle handle;

            /** @brief Holds the record of the version's producer. */
            ProducerRecord producer;
        };

        /**
         * @brief Keeps, per handle, the node that wrote its current version and who holds it.
         *
         * A dispatch records a version the worker alone holds until its result settles on the
         * master; a local run or a settled result names the master as holder. When a worker is
         * lost, lose() names every current version it alone held, in handle order, so the
         * master rebuilds each by running its producer again. One mutex covers the table.
         */
        class SUSHIRUNTIME_API ProducerTable final
        {
            public:
                /** @brief Builds an empty table. */
                ProducerTable() = default;

                ProducerTable(const ProducerTable&) = delete;
                ProducerTable& operator=(const ProducerTable&) = delete;
                ProducerTable(ProducerTable&&) = delete;
                ProducerTable& operator=(ProducerTable&&) = delete;

                /**
                 * @brief Records that task @p task_id on @p worker writes each output at its
                 *        version. A record of a newer version is kept.
                 */
                void record_remote(SushiRuntime::span<const BufferHandle> outputs,
                                   Internal::Graph::Node* node, std::uint64_t task_id,
                                   PeerID worker);

                /** @brief Records that the master wrote each output itself. */
                void record_local(SushiRuntime::span<const BufferHandle> outputs,
                                  Internal::Graph::Node* node);

                /**
                 * @brief Names the master as holder of the outputs of a whole result.
                 *
                 * An output whose record names another task or version is left alone.
                 */
                void settle(std::uint64_t task_id,
                            SushiRuntime::span<const BufferHandle> outputs) noexcept;

                /**
                 * @brief Takes every current version only @p worker held, in handle order.
                 * @param out Cleared first. The records keep their producer and name the master
                 *            as holder; the rebuild records again when the producer runs.
                 */
                void lose(PeerID worker, std::vector<LostInstance>& out);

                /** @brief Returns the record of a handle's current version, or nothing. */
                [[nodiscard]] std::optional<ProducerRecord> producer_of(GlobalHandle handle) const;

                /** @brief Forgets a handle that is no longer shared. */
                void forget(GlobalHandle handle) noexcept;

            private:
                /**
                 * @brief Stores @p record for @p handle unless a newer version is recorded.
                 * @pre The mutex is held.
                 */
                void store(GlobalHandle handle, const ProducerRecord& record);

                /// @brief Guards the table.
                mutable std::mutex mutex_;

                /// @brief Records by handle, in handle order.
                std::map<GlobalHandle, ProducerRecord> records_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
