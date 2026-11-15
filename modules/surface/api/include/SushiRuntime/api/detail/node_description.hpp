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
#include <optional>
#include <variant>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/late_controls.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
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
         * @brief Says whether a described node's command group submits the same work at every
         *        step, which decides whether a recorded command graph may keep it.
         * @see docs/design/GRAPH_REPLAY/README.md
         */
        enum class SubmitForm : std::uint8_t
        {
            StepBound,   /**< It may read host state that changes between steps. */
            Fixed,       /**< It submits the same work at every step. */
            LateWindow,  /**< A late-bound window; recorded at capacity, it reads its controls from host words. */
            DeviceSized  /**< It launches at capacity and reads its width from a word a predecessor wrote. */
        };

        /**
         * @brief Holds the handles a State rule reads and writes at its next step.
         *
         * Both lists are null for a rule that names no operation.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        struct LiveFields
        {
            /** @brief The current field of each State the rule reads, the one written first. */
            std::shared_ptr<std::vector<GlobalHandle>> reads;

            /** @brief The next field of the State the rule writes. */
            std::shared_ptr<std::vector<GlobalHandle>> writes;
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

            /**
             * @brief The launch run as a host loop, for a runtime with no device; empty for an
             *        NdRange, a raw command group, a queue body and a host node.
             */
            WorkerWork host_form;

            /** @brief Launch width baked on the node; K_WIDTH_UNKNOWN leaves it unset. */
            std::size_t width = K_WIDTH_UNKNOWN;

            /** @brief What a recorded command graph may keep of the submission; a verb that sets none is StepBound. */
            SubmitForm submit_form = SubmitForm::StepBound;

            /**
             * @brief Untracked and Task: index of the device the node is placed on; a Task that
             *        states none is placed by the device that owns its memory.
             */
            std::optional<std::size_t> device = std::nullopt;

            /** @brief Dynamic and DynamicState: the per-step controls of the node. */
            std::shared_ptr<LateControls> bindings;

            /** @brief State and DynamicState: names the regions at each plan build. */
            StateAccess state_access;

            /** @brief State and DynamicState: the live fields of a rule that names an operation. */
            LiveFields live;

            /**
             * @brief Kernel of a capture launch, or task bound by handle: the buffer of each pointer
             *        the work receives, in order; empty for every other node.
             */
            std::vector<GlobalHandle> pointer_handles;

            /**
             * @brief The home address of each pointer in pointer_handles, recorded at add(); a
             *        task bound by handle leaves it empty and the directory gives each home.
             */
            std::vector<void*> pointer_homes;

            /**
             * @brief True when every address the kernel reads comes from pointer_handles, so the
             *        node may run on a device other than the home of its data; a capture launch
             *        and a task bound by handle set it.
             */
            bool rebindable = false;
        };
    } // namespace Detail
} // namespace SushiRuntime
