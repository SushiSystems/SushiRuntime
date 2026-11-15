/**************************************************************************/
/* partition_refusal.hpp                                                  */
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

/**
 * @file partition_refusal.hpp
 * @brief Declares PoolShortfall and PartitionRefusal, why a job was refused before loading.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds the bytes one pool was asked to hold and the bytes it can hold. */
        struct PoolShortfall
        {
            std::uint32_t pool = 0;       /**< The pool, as PartitionDevice::pool names it. */
            std::uint64_t required = 0;   /**< Bytes the refused plan put in the pool. */
            std::uint64_t usable = 0;     /**< Capacity of the pool less its reserve. */

            /** @brief Returns the bytes required exceeds usable by, or zero. */
            [[nodiscard]] std::uint64_t missing() const noexcept
            {
                return required > usable ? required - usable : 0;
            }
        };

        /** @brief Holds the error of a refused job and, for a refusal on capacity, one row per pool. */
        struct PartitionRefusal
        {
            Error error;                      /**< Class and sentence of the refusal. */
            std::vector<PoolShortfall> pools; /**< One row per pool in pool order; empty when capacity was not the cause. */

            /**
             * @brief Returns the refusal as one error: its class, its sentence and, per pool, the
             *        bytes required, the bytes usable and the bytes missing.
             */
            [[nodiscard]] Error to_error() const
            {
                std::string message = error.message;
                for (const PoolShortfall& row : pools)
                {
                    message += "; pool " + std::to_string(row.pool) + " needs " + std::to_string(row.required) +
                               " bytes of " + std::to_string(row.usable) + " usable, " +
                               std::to_string(row.missing()) + " missing";
                }
                return Error{error.code, message};
            }
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
