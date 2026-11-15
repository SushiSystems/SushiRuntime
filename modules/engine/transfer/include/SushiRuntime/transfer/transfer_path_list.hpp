/**************************************************************************/
/* transfer_path_list.hpp                                                 */
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
 * @file transfer_path_list.hpp
 * @brief Declares admit_path(), which adds a transfer path to a list unless its identity is taken.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <vector>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/transfer/transfer_path.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Names what admit_path() decided for a path. */
        enum class PathListStatus : std::uint8_t
        {
            Admitted,    /**< The path was added to the list. */
            DuplicateID, /**< A path of the list has the same TransferPathID; the path was not added. */
        };

        /**
         * @brief Adds path to list when no path of list has its TransferPathID.
         *
         * Selecting, pricing and counting a path key on its id, so two paths with one id would be
         * taken for each other. The refused path is logged at error level with both names.
         * @param list The paths in the order they are tried.
         * @param path The path to add; the list does not own it.
         */
        [[nodiscard]] PathListStatus admit_path(std::vector<TransferPath*>& list, TransferPath& path);

        /**
         * @brief Builds list from candidates with admit_path(), in order.
         * @param candidates Non-null paths; the list does not own them.
         * @return Ok when every candidate was admitted, else InvalidState naming the first
         *         refused path; the candidates before and after it are still admitted.
         */
        [[nodiscard]] Result<void> admit_paths(const std::vector<TransferPath*>& candidates,
                                               std::vector<TransferPath*>& list);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
