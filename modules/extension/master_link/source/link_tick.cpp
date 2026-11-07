/**************************************************************************/
/* link_tick.cpp                                                          */
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
 * @file link_tick.cpp
 * @brief Implements the pure step of the link's thread: what is due at a given time.
 * @author Mustafa Garip
 * @see modules/extension/master_link/README.md
 */

#include <SushiRuntime/master_link/link_tick.hpp>

#include <algorithm>

namespace SushiRuntime
{
    namespace Distributed
    {
        LinkTick plan_link_tick(const LinkTickInput& input,
                                std::chrono::steady_clock::time_point now) noexcept
        {
            LinkTick tick;
            tick.phase = input.phase;
            tick.transport_stopped = input.transport_stopped;
            tick.heartbeat_at = input.heartbeat_at;
            tick.hello_at = input.hello_at;

            if (input.ended && !input.transport_stopped)
            {
                tick.action = LinkAction::StopTransport;
                tick.transport_stopped = true;
            }
            else if (input.running && input.phase == LinkPhase::Registered)
            {
                const bool watched = input.liveness > std::chrono::milliseconds::zero();
                if (watched && now - input.last_frame >= input.liveness)
                {
                    tick.action = LinkAction::CloseSilent;
                    tick.phase = LinkPhase::Closing;
                    tick.silence = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - input.last_frame);
                    tick.limit = input.liveness;
                }
                else if (now >= input.heartbeat_at)
                {
                    tick.action = LinkAction::Heartbeat;
                    tick.heartbeat_at = now + input.heartbeat_interval;
                }
                else
                {
                    tick.wake = watched ? std::min(input.heartbeat_at,
                                                   input.last_frame + input.liveness)
                                        : input.heartbeat_at;
                }
            }
            else if (input.running && (input.phase == LinkPhase::HelloSent ||
                                       input.phase == LinkPhase::ProofSent))
            {
                if (now - input.hello_at >= input.heartbeat_interval)
                {
                    tick.action = LinkAction::Restart;
                    tick.hello_at = now;
                }
                else
                {
                    tick.wake = input.hello_at + input.heartbeat_interval;
                }
            }
            return tick;
        }

    } // namespace Distributed
} // namespace SushiRuntime
