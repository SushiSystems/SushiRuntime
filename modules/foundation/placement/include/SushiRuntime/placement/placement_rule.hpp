/**************************************************************************/
/* placement_rule.hpp                                                     */
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
 * @file placement_rule.hpp
 * @brief Declares PlacementRule and the plain records it is made of.
 * @author Mustafa Garip
 * @see modules/foundation/placement/README.md
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <SushiRuntime/machine/device_kind.hpp>
#include <SushiRuntime/machine/node_label.hpp>

namespace SushiRuntime
{
    /**
     * @brief Names what happens to a node whose target is absent.
     */
    enum class AbsentAction : std::uint8_t
    {
        Local, /**< The node runs where it would run without the rule. */
        Wait,  /**< The node waits for the target, for at most the policy's bound. */
        Fail   /**< The run ends with an error that names the rule. */
    };

    /**
     * @brief Holds the action for an absent target and the bound of a wait.
     */
    struct AbsentPolicy
    {
        AbsentAction action = AbsentAction::Local; /**< What happens when the target is absent. */
        std::chrono::milliseconds bound{0};        /**< Longest wait; read only for Wait. */
    };

    /**
     * @brief States which nodes a rule applies to; each part left empty matches any node.
     */
    struct PlacementMatch
    {
        /** @brief "a.b" for one class, "a.b.*" for every class below "a.b", empty for any. */
        std::string placement_class;

        /** @brief The placement key a node must carry; none matches any node. */
        std::optional<std::uint64_t> key;

        /** @brief The name of the operation a node must run; empty matches any operation. */
        std::string operation;
    };

    /**
     * @brief States where a matching node runs: a machine, then a device on it.
     */
    struct PlacementTarget
    {
        /** @brief The machine's name; empty states no name. */
        std::string machine_name;

        /** @brief A label the machine must carry; none states no label. */
        std::optional<NodeLabel> machine_label;

        /** @brief The position of the device on the machine. */
        std::optional<std::size_t> device_index;

        /** @brief The class of the device. */
        std::optional<DeviceKind> device_kind;

        /** @brief The least memory in bytes the device must have. */
        std::optional<std::uint64_t> device_min_memory;
    };

    /**
     * @brief Binds the nodes a match selects to a target, with a priority and a policy.
     * @see modules/foundation/placement/README.md
     */
    struct PlacementRule
    {
        std::string name;        /**< Names the rule in an error; may be empty. */
        PlacementMatch match;    /**< Selects the nodes the rule applies to. */
        PlacementTarget target;  /**< Says where those nodes run. */
        int priority = 0;        /**< The larger value wins a conflict. */
        AbsentPolicy if_absent;  /**< What happens when the target is absent. */

        /** @brief Overrides the cluster's task deadline for the nodes the rule places. */
        std::optional<std::chrono::milliseconds> deadline;
    };
} // namespace SushiRuntime
