/**************************************************************************/
/* guarded_transfer_path.hpp                                              */
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
 * @file guarded_transfer_path.hpp
 * @brief Declares make_guarded_transfer_path(), a TransferPath whose copies run with the device gate open.
 * @author Mustafa Garip
 * @see modules/engine/transfer/README.md
 */

#pragma once

#include <memory>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/device_access_gate.hpp>
#include <SushiRuntime/transfer/transfer_path.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Wraps a path so each copy opens the gate, waits for its event and closes the gate.
         * @param inner Not null; the path the result owns and forwards to.
         * @param gate Opened around each copy; must outlive the result.
         * @see modules/engine/transfer/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API std::unique_ptr<TransferPath>
        make_guarded_transfer_path(std::unique_ptr<TransferPath> inner, Memory::DeviceAccessGate& gate);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
