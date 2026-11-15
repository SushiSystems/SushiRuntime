/**************************************************************************/
/* profile_store.cpp                                                      */
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
 * @file profile_store.cpp
 * @brief Implements ProfileStore.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partition_session/profile_store.hpp>

#include <utility>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/device_profile/profile_fold.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        ProfileStore::ProfileStore(std::shared_ptr<const CostModel> model) noexcept : model_(std::move(model))
        {
        }

        std::shared_ptr<const CostModel> ProfileStore::model() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return model_;
        }

        std::shared_ptr<const CostModel> ProfileStore::fold(const std::vector<DispatchObservation>& observed)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            SR_VERIFY(model_ != nullptr, "ProfileStore::fold(): the store holds no profiles; check model().");
            if (!observed.empty())
                model_ = std::make_shared<const CostModel>(fold_observations(model_->profiles(), observed));
            return model_;
        }

        Result<bool> ProfileStore::fold_into_file(PartitionLedger& ledger, const std::filesystem::path& path)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return ledger.fold_into(path);
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
