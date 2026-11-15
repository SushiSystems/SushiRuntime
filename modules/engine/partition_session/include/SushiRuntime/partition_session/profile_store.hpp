/**************************************************************************/
/* profile_store.hpp                                                      */
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
 * @file profile_store.hpp
 * @brief Declares ProfileStore, the one owner of a runtime's device profiles in memory and in its profile file.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device_profile/cost_model.hpp>
#include <SushiRuntime/device_profile/dispatch_observation.hpp>
#include <SushiRuntime/partition_session/partition_ledger.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Holds the cost model every session of a runtime prices with, and folds what each measured into it.
         *
         * The sessions of two graphs may reach a horizon at once, so each fold, in memory or into the
         * profile file, takes the store's lock. A fold happens at a horizon, never per dispatch.
         */
        class ProfileStore final
        {
            public:
                /** @brief Builds a store over model; null holds no profiles, and no session plans on its own. */
                explicit ProfileStore(std::shared_ptr<const CostModel> model) noexcept;

                /** @brief Returns the model in memory: the one read at create with every fold since; null when none. */
                [[nodiscard]] std::shared_ptr<const CostModel> model() const;

                /**
                 * @brief Folds observed into the model in memory and returns the new model.
                 * @pre model() is not null.
                 */
                [[nodiscard]] std::shared_ptr<const CostModel> fold(const std::vector<DispatchObservation>& observed);

                /** @brief Folds what ledger holds unfolded into the profile file at path; see PartitionLedger::fold_into(). */
                [[nodiscard]] Result<bool> fold_into_file(PartitionLedger& ledger, const std::filesystem::path& path);

            private:
                /// @brief Guards model_ and the profile file.
                mutable std::mutex mutex_;

                /// @brief The model in memory; null when no profile file was read.
                std::shared_ptr<const CostModel> model_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
