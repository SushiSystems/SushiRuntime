/**************************************************************************/
/* description_runner.hpp                                                 */
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
 * @file description_runner.hpp
 * @brief Declares run_description(), which runs a described node's work once on a runtime.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_dynamic.hpp>
#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/api/detail/step_machinery.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>

namespace SushiRuntime
{
    namespace Testing
    {
        /** @brief Returns the runtime's first device queue, on whose context its memory lives. */
        inline sycl::queue& queue_of(const Runtime& runtime)
        {
            return runtime.advanced().queue(DeviceIndex{0});
        }

        /** @brief Runs the work of a described node once on the runtime's first queue and waits. */
        inline void run_description(const Runtime& runtime, const Detail::NodeDescription& description)
        {
            sycl::queue& queue = queue_of(runtime);
            std::visit(
                [&queue](const auto& work)
                {
                    using Work = std::decay_t<decltype(work)>;
                    if constexpr (std::is_same_v<Work, Detail::CommandGroupWork>)
                        queue.submit(work).wait();
                    else if constexpr (std::is_same_v<Work, Detail::QueueWork>)
                        work(queue, std::vector<sycl::event>{}).wait();
                    else
                        work();
                },
                description.work);
        }

        /**
         * @brief Builds the controls of a late-bound node for @p dynamic and applies the first step's hooks.
         * @return Controls whose count and base are the values the providers of @p dynamic returned.
         */
        inline std::shared_ptr<Detail::LateControls> bound_controls(Dynamic dynamic)
        {
            std::vector<BufferRegion> reads;
            std::vector<LivenessObserver> lives;
            std::shared_ptr<Detail::LateControls> controls =
                Detail::make_late_bindings(dynamic, reads, lives);
            Detail::StepMachinery machinery;
            machinery.register_binding_updater(controls, std::move(dynamic));
            machinery.before_step();
            return controls;
        }
    } // namespace Testing
} // namespace SushiRuntime
