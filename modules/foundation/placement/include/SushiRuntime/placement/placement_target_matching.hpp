/**************************************************************************/
/* placement_target_matching.hpp                                          */
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
 * @file placement_target_matching.hpp
 * @brief Declares the match of a rule's target against the facts of one machine.
 * @author Mustafa Garip
 * @see modules/foundation/placement/README.md
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/placement/placement_rule.hpp>

namespace SushiRuntime
{
    /** @brief Holds the device a target with no device part names. */
    inline constexpr std::size_t K_DEFAULT_PLACEMENT_DEVICE = 0;

    /**
     * @brief Returns whether two machine names are equal in their canonical form.
     *
     * The canonical form is the first DNS label, the text before the first '.', in ASCII
     * lower case, so "Rack-7" and "rack-7.lab.example" are one machine.
     * @see modules/foundation/placement/README.md
     */
    [[nodiscard]] SUSHIRUNTIME_API bool machine_names_match(std::string_view first,
                                                            std::string_view second) noexcept;

    /**
     * @brief Returns whether the machine part of a target names a machine.
     *
     * A target with no machine part names every machine. A name matches NodeFacts::name by
     * machine_names_match(); a label matches when the machine carries that key with that value.
     */
    [[nodiscard]] SUSHIRUNTIME_API bool machine_matches(const PlacementTarget& target,
                                                        const NodeFacts& machine) noexcept;

    /**
     * @brief Returns the device of a machine the device part of a target picks.
     *
     * The device at that index, the first device of that kind, or the first device with at
     * least that much memory. A target with no device part picks K_DEFAULT_PLACEMENT_DEVICE.
     * The machine part is not read.
     * @return The position in NodeFacts::devices, or none when the machine has no such device.
     */
    [[nodiscard]] SUSHIRUNTIME_API std::optional<std::size_t>
    pick_device(const PlacementTarget& target, const NodeFacts& machine) noexcept;

    /**
     * @brief Returns the device of a machine a target puts a node on.
     *
     * The target names the machine only when its machine part matches and its device part
     * picks a device there; every caller that asks where a target places a node asks this.
     * @return The position in NodeFacts::devices, or none when the target does not name it.
     * @see modules/foundation/placement/README.md
     */
    [[nodiscard]] SUSHIRUNTIME_API std::optional<std::size_t>
    device_on(const PlacementTarget& target, const NodeFacts& machine) noexcept;

    /** @brief Returns a rule's name in quotes, or "at position N" when it has none. */
    [[nodiscard]] SUSHIRUNTIME_API std::string describe_rule(const PlacementRule& rule,
                                                             std::size_t position);

    /** @brief Returns every part a target states, as a phrase for a message. */
    [[nodiscard]] SUSHIRUNTIME_API std::string describe_target(const PlacementTarget& target);
} // namespace SushiRuntime
