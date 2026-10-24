/**************************************************************************/
/* placement_scope.hpp                                                    */
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

/**
 * @file placement_scope.hpp
 * @brief Declares PlacementScope, the class and key that Recorder::within() stamps on nodes.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#pragma once

#include <cstdint>
#include <optional>

#include <SushiRuntime/core/placement_class.hpp>

namespace SushiRuntime
{
    /**
     * @brief Names a placement class and an optional key, as one copyable value.
     *
     * Runtime::placement_scope() builds one after it checks the class name. The value holds no
     * name, device or memory, so a copy is a few words and outlives the Runtime that made it.
     * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
     */
    class PlacementScope
    {
        public:
            /**
             * @brief Holds a class and a key.
             * @param placement_class Class every node recorded in the scope takes.
             * @param placement_key   Key every such node takes; 0 is a key like any other.
             */
            constexpr PlacementScope(PlacementClass placement_class,
                                     std::optional<std::uint64_t> placement_key) noexcept
                : placement_class_(placement_class), placement_key_(placement_key)
            {
            }

            /** @brief Returns the class the scope names. */
            [[nodiscard]] constexpr PlacementClass placement_class() const noexcept
            {
                return placement_class_;
            }

            /** @brief Returns the key the scope names, or none. */
            [[nodiscard]] constexpr std::optional<std::uint64_t> placement_key() const noexcept
            {
                return placement_key_;
            }

        private:
            /** @brief The class the scope names. */
            PlacementClass placement_class_;

            /** @brief The key the scope names, or none. */
            std::optional<std::uint64_t> placement_key_;
    };
} // namespace SushiRuntime
