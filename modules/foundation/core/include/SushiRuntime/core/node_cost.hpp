/**************************************************************************/
/* node_cost.hpp                                                          */
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
 * @file node_cost.hpp
 * @brief Declares NodeCost and NodeDType, the work a caller states one dispatch of a node does.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <cstdint>
#include <type_traits>

#include <SushiRuntime/core/operation_kind.hpp>

namespace SushiRuntime
{
    /** @brief Names the element type a node's arithmetic runs on. */
    enum class NodeDType : std::uint8_t
    {
        Unstated, /**< The caller named none. */
        F32,      /**< 32-bit floating point. */
        F16,      /**< 16-bit floating point. */
        BF16,     /**< 16-bit brain floating point. */
        I8        /**< 8-bit signed integer. */
    };

    /**
     * @brief Returns the name a record carries for @p dtype, or nullptr for Unstated.
     * @return "f32", "f16", "bf16" or "i8".
     */
    [[nodiscard]] constexpr const char* dtype_name(NodeDType dtype) noexcept
    {
        switch (dtype)
        {
            case NodeDType::F32:
                return "f32";
            case NodeDType::F16:
                return "f16";
            case NodeDType::BF16:
                return "bf16";
            case NodeDType::I8:
                return "i8";
            case NodeDType::Unstated:
                break;
        }
        return nullptr;
    }

    /**
     * @brief Holds the operations, the bytes read, the bytes written, the operation kind and the element type of one dispatch of a node.
     *
     * A caller sets it as an std::optional on NodeOptions; absent means the caller stated no cost, and a
     * stated cost may hold zeros, since a data-movement node does no arithmetic. The runtime carries
     * the figures to whoever observes the run, and a cost model keys a throughput on the kind.
     * @see modules/foundation/core/README.md
     */
    struct NodeCost
    {
        std::uint64_t operations = 0;                     /**< Operations of one dispatch, counted in the unit of its kind. */
        std::uint64_t read_bytes = 0;                     /**< Bytes one dispatch reads. */
        std::uint64_t written_bytes = 0;                  /**< Bytes one dispatch writes. */
        OperationKind kind = OperationKinds::K_GENERIC;   /**< Class of work the operations belong to. */
        NodeDType dtype = NodeDType::Unstated;            /**< Element type of the arithmetic, when the caller names it. */

        /** @brief Returns the bytes one dispatch reads and writes together. */
        [[nodiscard]] constexpr std::uint64_t moved_bytes() const noexcept
        {
            return read_bytes + written_bytes;
        }

        /** @brief Returns whether every field is equal. */
        [[nodiscard]] constexpr bool operator==(const NodeCost& other) const noexcept
        {
            return operations == other.operations && read_bytes == other.read_bytes &&
                   written_bytes == other.written_bytes && kind == other.kind && dtype == other.dtype;
        }

        /** @brief Returns whether a field differs. */
        [[nodiscard]] constexpr bool operator!=(const NodeCost& other) const noexcept
        {
            return !(*this == other);
        }
    };

    static_assert(std::is_trivially_copyable_v<NodeCost>, "NodeCost is copied by value into every node and record.");
} // namespace SushiRuntime
