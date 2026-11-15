/**************************************************************************/
/* transfer_path_list.cpp                                                 */
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
 * @file transfer_path_list.cpp
 * @brief Implements admit_path().
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/transfer/transfer_path_list.hpp>

#include <string>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/transfer/transfer_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        PathListStatus admit_path(std::vector<TransferPath*>& list, TransferPath& path)
        {
            for (const TransferPath* const held : list)
            {
                if (held->id() != path.id())
                    continue;
                SR_LOG_ERROR(K_TRANSFER_LOG_CATEGORY,
                             "The transfer path '{}' was refused: its id equals the id of '{}'.",
                             path.name(), held->name());
                return PathListStatus::DuplicateID;
            }
            list.push_back(&path);
            return PathListStatus::Admitted;
        }

        Result<void> admit_paths(const std::vector<TransferPath*>& candidates,
                                 std::vector<TransferPath*>& list)
        {
            std::string refused;
            for (TransferPath* const candidate : candidates)
            {
                if (admit_path(list, *candidate) == PathListStatus::Admitted || !refused.empty())
                    continue;
                refused = std::string(candidate->name());
            }
            if (refused.empty())
                return {};
            return make_error(Error{ErrorCode::InvalidState,
                                    "The transfer path '" + refused +
                                        "' has the id of a path already listed."});
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
