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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file node_cost.hpp
 * @brief Declares NodeCost and NodeDType, the work a caller states one dispatch of a node does.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <cstdint>

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
     * @brief Holds the floating-point operations, the bytes moved and the element type of one dispatch of a node.
     *
     * A caller sets it as an std::optional on NodeOptions; absent means the caller stated no cost, and a
     * stated cost may hold zeros, since a data-movement node does no floating-point work. The runtime
     * only carries the figures to whoever observes the run; it computes nothing from them.
     * @see modules/foundation/core/README.md
     */
    struct NodeCost
    {
        std::uint64_t flops = 0;                 /**< Floating-point operations of one dispatch. */
        std::uint64_t bytes = 0;                 /**< Bytes one dispatch reads and writes. */
        NodeDType dtype = NodeDType::Unstated;   /**< Element type of the arithmetic, when the caller names it. */

        /** @brief Returns whether every field is equal. */
        [[nodiscard]] constexpr bool operator==(const NodeCost& other) const noexcept
        {
            return flops == other.flops && bytes == other.bytes && dtype == other.dtype;
        }

        /** @brief Returns whether a field differs. */
        [[nodiscard]] constexpr bool operator!=(const NodeCost& other) const noexcept
        {
            return !(*this == other);
        }
    };
} // namespace SushiRuntime
