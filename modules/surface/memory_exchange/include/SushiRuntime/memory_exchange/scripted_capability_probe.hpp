/**************************************************************************/
/* scripted_capability_probe.hpp                                          */
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
 * @file scripted_capability_probe.hpp
 * @brief Declares ScriptedCapabilityProbe, a CapabilityCache::Probe fed by the caller.
 * @author Mustafa Garip
 * @see modules/surface/memory_exchange/README.md
 */

#pragma once

#include <cstddef>
#include <deque>
#include <stdexcept>
#include <string>
#include <utility>

#include <sycl/sycl.hpp>

#include <SushiRuntime/interop/interop_capabilities.hpp>

namespace SushiRuntime
{
    namespace Internal::MemoryExchange
    {
        /**
         * @brief Answers each probe with the next queued step, and counts the probes.
         *
         * Pass it to CapabilityCache as std::ref(probe), so the caller keeps the count.
         * The device a probe names is not read; next_answer() needs no SYCL device;
         * operator() forwards to it.
         * @see modules/surface/memory_exchange/README.md
         */
        class ScriptedCapabilityProbe final
        {
            public:
                /**
                 * @brief Queues the answer of the next probe that has none yet.
                 * @param answer What that probe returns.
                 */
                void answer_next(InteropCapabilities answer)
                {
                    script_.push_back(Step{std::move(answer), std::string{}, false});
                }

                /**
                 * @brief Queues a throw for the next probe that has no step yet.
                 * @param message The message of the std::runtime_error that probe throws.
                 */
                void throw_next(std::string message)
                {
                    script_.push_back(Step{InteropCapabilities{}, std::move(message), true});
                }

                /**
                 * @brief Returns the probes asked so far, including those that threw.
                 * @return The call count.
                 */
                [[nodiscard]] std::size_t calls() const noexcept
                {
                    return calls_;
                }

                /**
                 * @brief Takes the next queued step and answers from it.
                 * @return The queued answer.
                 * @throws std::runtime_error when the step queued is a throw.
                 * @throws std::logic_error when no step is queued, so an extra call fails.
                 */
                [[nodiscard]] InteropCapabilities next_answer()
                {
                    ++calls_;
                    if (script_.empty())
                        throw std::logic_error("ScriptedCapabilityProbe: no answer was queued");
                    Step step = std::move(script_.front());
                    script_.pop_front();
                    if (step.fails)
                        throw std::runtime_error(step.error);
                    return step.answer;
                }

                /**
                 * @brief Answers a probe of @p device from the queue, without reading it.
                 * @param device The device the probe asks about.
                 * @return The queued answer.
                 */
                [[nodiscard]] InteropCapabilities operator()(const sycl::device& device)
                {
                    static_cast<void>(device);
                    return next_answer();
                }

            private:
                /** @brief One queued probe: an answer, or a throw when fails is set. */
                struct Step
                {
                    InteropCapabilities answer; /**< What the probe returns. */
                    std::string error;          /**< The message of the throw. */
                    bool fails = false;         /**< Whether the probe throws. */
                };

                /** @brief The steps not yet taken, oldest first. */
                std::deque<Step> script_;

                /** @brief Probes asked so far. */
                std::size_t calls_ = 0;
        };
    } // namespace Internal::MemoryExchange
} // namespace SushiRuntime
