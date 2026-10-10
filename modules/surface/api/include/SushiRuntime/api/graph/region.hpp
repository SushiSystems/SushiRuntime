/**************************************************************************/
/* region.hpp                                                             */
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
 * @file region.hpp
 * @brief Declares Region, one mutable partition of a DynamicGraph.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

#include <SushiRuntime/api/graph/builder_surface.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/api/primitives/primitive_members.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Records one mutable partition of a DynamicGraph through the add() surface.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        class Region : public BuilderSurface<Region>,
                       public Primitives::PrimitiveMembers<Region>
        {
            public:
                /**
                 * @brief Starts an empty region on a runtime context.
                 * @param ctx Context the region's recorder allocates scratch from.
                 */
                explicit Region(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : recorder_(std::move(ctx))
                {
                }

                /** @brief Returns the recorder this region's tasks are recorded on. */
                [[nodiscard]] Recorder& recorder() noexcept { return recorder_; }

                /** @brief Returns the recorder this region's tasks are recorded on, read-only. */
                [[nodiscard]] const Recorder& recorder() const noexcept { return recorder_; }

                /**
                 * @brief Reports how much work has been recorded.
                 * @return Number of update rules (tasks) added so far.
                 */
                std::size_t size() const noexcept { return recorder_.node_count(); }

            private:
                /** @brief Owns the recorded tasks, their scratch and their per-step hooks. */
                Recorder recorder_;
        };

    } // namespace API
} // namespace SushiRuntime
