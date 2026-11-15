/**************************************************************************/
/* handle_ports.hpp                                                       */
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
 * @file handle_ports.hpp
 * @brief Declares HandlePorts, the references a consumer takes to reach the handle table by role.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <SushiRuntime/memory/coherence_history.hpp>
#include <SushiRuntime/memory/handle_registry.hpp>
#include <SushiRuntime/memory/host_write_log.hpp>
#include <SushiRuntime/memory/residency_view.hpp>
#include <SushiRuntime/memory/retire_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Names the roles a handle table plays, so a consumer takes the ports it uses and
         *        never the table's concrete class.
         *
         * The references stay valid for the life of the table that produced them.
         */
        struct HandlePorts
        {
            HandleRegistry& registry;      /**< Mints, describes and retires handles. */
            ResidencyView& residency;      /**< Reads and writes what each handle holds where. */
            HostWriteLog& host_writes;     /**< Records and reads the host writes outside a plan. */
            CoherenceHistory& history;     /**< Drops the history of retired steps. */
            RetireSource& retirement;      /**< Tells an observer about each retired handle. */
        };

    } // namespace Internal::Memory
} // namespace SushiRuntime
