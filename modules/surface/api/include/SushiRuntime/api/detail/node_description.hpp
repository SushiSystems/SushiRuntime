/**************************************************************************/
/* node_description.hpp                                                   */
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
 * @file node_description.hpp
 * @brief Declares NodeDescription, what a recording verb hands the library for one node.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <variant>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/late_controls.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/report/dispatch_width.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief Stands for "this rule has no registration index yet". */
        inline constexpr std::uint32_t K_NO_REGISTRATION = static_cast<std::uint32_t>(-1);

        /** @brief Work of a node that fills one SYCL command group. */
        using CommandGroupWork = std::function<void(sycl::handler&)>;

        /** @brief Work of a node that submits on a queue and returns the event that ends it. */
        using QueueWork =
            std::function<sycl::event(sycl::queue&, const std::vector<sycl::event>&)>;

        /** @brief Work of a node that runs once on a worker thread. */
        using WorkerWork = std::function<void()>;

        /**
         * @brief Names how the library adds a described node to a plan.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        enum class NodeKind : std::uint8_t
        {
            Kernel,       /**< One command group ordered by the regions it names. */
            Untracked,    /**< One command group that names no region, placed on a device. */
            Task,         /**< A body that submits on the queue it is handed. */
            Host,         /**< A body run once on a worker thread. */
            State,        /**< A rule of a State; its regions are read at each plan build. */
            Dynamic,      /**< A command group whose window is bound per step. */
            DynamicState, /**< A rule of a State whose window is bound per step. */
        };

        /**
         * @brief Holds the addresses a State rule reads and writes at its next step.
         *
         * Both lists are null for a rule that names no operation.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        struct LiveFields
        {
            /** @brief The current field of each State the rule reads, the one written first. */
            std::shared_ptr<std::vector<void*>> reads;

            /** @brief The next field of the State the rule writes. */
            std::shared_ptr<std::vector<void*>> writes;
        };

        /**
         * @brief Describes one recorded node with the type of its kernel erased.
         *
         * A recording verb fills it once; the library reads it at every plan build.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        struct NodeDescription
        {
            /** @brief Fills the regions of a State rule from the State's fields. */
            using StateAccess = std::function<void(std::vector<BufferRegion>& reads,
                                                   std::vector<BufferRegion>& writes)>;

            /** @brief How the node is added; it selects which fields below are read. */
            NodeKind kind = NodeKind::Kernel;

            /** @brief Regions read; Untracked and the State kinds leave it empty. */
            std::vector<BufferRegion> reads;

            /** @brief Regions written; Untracked and the State kinds leave it empty. */
            std::vector<BufferRegion> writes;

            /** @brief The work: QueueWork for Task, WorkerWork for Host, else a command group. */
            std::variant<CommandGroupWork, QueueWork, WorkerWork> work;

            /** @brief Launch width baked on the node; K_WIDTH_UNKNOWN leaves it unset. */
            std::size_t width = K_WIDTH_UNKNOWN;

            /** @brief Untracked: index of the device the node is placed on. */
            std::size_t device = 0;

            /** @brief Dynamic and DynamicState: the per-step controls of the node. */
            std::shared_ptr<LateControls> bindings;

            /** @brief State and DynamicState: names the regions at each plan build. */
            StateAccess state_access;

            /** @brief State and DynamicState: the live fields of a rule that names an operation. */
            LiveFields live;
        };
    } // namespace Detail
} // namespace SushiRuntime
