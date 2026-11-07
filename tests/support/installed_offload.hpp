/**************************************************************************/
/* installed_offload.hpp                                                  */
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
 * @file installed_offload.hpp
 * @brief Declares InstalledOffload, which installs an offload coordinator on a runtime for one scope.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>

#include <SushiRuntime/sushiruntime.hpp>

namespace TestSupport
{

/**
 * @brief Installs a new coordinator on a runtime through AdvancedControls::install_offload(), and removes it
 *        on exit, so the program's shared runtime runs with none again.
 * @tparam Coordinator An OffloadCoordinator that can be built with no argument.
 */
template <class Coordinator>
class InstalledOffload
{
    public:
        /**
         * @brief Installs a new coordinator on @p runtime.
         * @pre No run of @p runtime is in flight.
         */
        explicit InstalledOffload(const SushiRuntime::Runtime& runtime)
            : controls_(runtime.advanced()), coordinator_(std::make_shared<Coordinator>())
        {
            controls_.install_offload(coordinator_);
        }

        /** @brief Removes the coordinator from the runtime. */
        ~InstalledOffload()
        {
            controls_.install_offload(nullptr);
        }

        InstalledOffload(const InstalledOffload&) = delete;
        InstalledOffload& operator=(const InstalledOffload&) = delete;
        InstalledOffload(InstalledOffload&&) = delete;
        InstalledOffload& operator=(InstalledOffload&&) = delete;

        /** @brief Returns the installed coordinator. */
        [[nodiscard]] const Coordinator& coordinator() const noexcept
        {
            return *coordinator_;
        }

    private:
        SushiRuntime::AdvancedControls controls_;    /**< The controls of the runtime it is installed on. */
        std::shared_ptr<Coordinator> coordinator_;   /**< The installed coordinator, co-owned with the runtime. */
};

} // namespace TestSupport
