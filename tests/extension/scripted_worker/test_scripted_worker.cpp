/**************************************************************************/
/* test_scripted_worker.cpp                                              */
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
 * @file test_scripted_worker.cpp
 * @brief Tests ScriptedWorker: on connect it sends a HELLO that names its facts.
 * @see tests/extension/scripted_worker/README.md
 * @author Mustafa Garip
 */

#include <chrono>

#include <gtest/gtest.h>

#include <SushiRuntime/in_process_transport/in_process_transport.hpp>
#include <SushiRuntime/scripted_worker/scripted_worker.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/transport/recording_transport_events.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    constexpr std::chrono::seconds K_LIMIT{10};

    TEST(Medium_ScriptedWorker, ConnectingSendsAHelloNamingItsFacts)
    {
        InProcessFabric fabric;
        const std::shared_ptr<Transport> master = InProcessTransport::make_master(fabric);
        RecordingTransportEvents master_events;
        ASSERT_TRUE(master->start(master_events).is_ok());
        ScriptedWorker worker(fabric, OperationID{}, DispatchAnswer::Accept);
        worker.report(NodeFacts{"rack-7", OperatingSystem::Linux, {}, {}});

        ASSERT_TRUE(worker.start());

        ASSERT_TRUE(master_events.wait_for_frames(MessageType::Hello, 1, K_LIMIT));
        Wire::HelloMessage hello;
        ASSERT_TRUE(Wire::HelloMessage::decode(
                        master_events.frames_of(MessageType::Hello)[0].payload, hello).ok());
        EXPECT_EQ(hello.node_name, "rack-7");
        master->stop();
    }
} // namespace
