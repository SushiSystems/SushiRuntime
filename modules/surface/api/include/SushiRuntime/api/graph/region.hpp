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

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <SushiRuntime/api/graph/builder_surface.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Lists the regions of one DynamicGraph whose recorder was reached for writing.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        class RegionChangeLog final
        {
            public:
                /**
                 * @brief Lists key, once between two settle() calls.
                 * @param stamp The caller's own mark of the interval it was last listed in.
                 */
                void note(std::uint64_t key, std::uint64_t& stamp)
                {
                    if (stamp == interval_)
                        return;
                    stamp = interval_;
                    keys_.push_back(key);
                }

                /** @brief Returns the keys listed since the last settle(). */
                [[nodiscard]] const std::vector<std::uint64_t>& keys() const noexcept
                {
                    return keys_;
                }

                /** @brief Empties the list; every region may be listed again. */
                void settle() noexcept
                {
                    keys_.clear();
                    ++interval_;
                }

            private:
                /** @brief Keys listed in the current interval, each once. */
                std::vector<std::uint64_t> keys_;

                /** @brief Identity of the current interval; never zero, so a new stamp differs. */
                std::uint64_t interval_ = 1;
        };

        /**
         * @brief Records one mutable partition of a DynamicGraph through the add() surface.
         *
         * A region is neither copied nor moved: its graph and its per-step hooks hold its address.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        class Region : public BuilderSurface<Region>
        {
            public:
                /**
                 * @brief Starts an empty region on a runtime context, owned by no graph.
                 * @param ctx Context the region's recorder allocates scratch from.
                 */
                explicit Region(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : recorder_(std::move(ctx))
                {
                }

                /**
                 * @brief Starts an empty region that tells its graph when it is written to.
                 * @param changes Log of the owning graph; must outlive this region.
                 * @param key     Identity the graph knows this region by.
                 */
                Region(std::shared_ptr<Execution::RuntimeContext> ctx, RegionChangeLog& changes,
                       std::uint64_t key)
                    : recorder_(std::move(ctx)), changes_(&changes), key_(key)
                {
                }

                Region(const Region&) = delete;
                Region& operator=(const Region&) = delete;
                Region(Region&&) = delete;
                Region& operator=(Region&&) = delete;

                /**
                 * @brief Returns the recorder to record on, and lists this region as written to.
                 * @pre The reference is not kept across a run; call recorder() again to record.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                [[nodiscard]] Recorder& recorder()
                {
                    if (changes_ != nullptr)
                        changes_->note(key_, noted_);
                    return recorder_;
                }

                /** @brief Returns the recorder this region's tasks are recorded on, read-only. */
                [[nodiscard]] const Recorder& recorder() const noexcept { return recorder_; }

                /**
                 * @brief Forgets everything recorded, so the region can be recorded afresh.
                 * @pre No step started with run_async() on the owning graph is pending.
                 */
                void clear() { recorder().clear(); }

            private:
                /** @brief Owns the recorded tasks, their scratch and their per-step hooks. */
                Recorder recorder_;

                /** @brief Log of the owning graph, or null for a region no graph owns. */
                RegionChangeLog* changes_ = nullptr;

                /** @brief Identity the owning graph knows this region by. */
                std::uint64_t key_ = 0;

                /** @brief Interval of changes_ this region was last listed in; zero for never. */
                std::uint64_t noted_ = 0;
        };

    } // namespace API
} // namespace SushiRuntime
