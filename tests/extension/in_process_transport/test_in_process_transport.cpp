/**************************************************************************/
/* test_in_process_transport.cpp                                          */
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
 * @file test_in_process_transport.cpp
 * @brief Tests InProcessTransport against the Transport contract: ordering, loss, stop and the statuses of a send, on threadless endpoints, and the stop of a threaded one.
 * @see tests/extension/in_process_transport/README.md
 * @author Mustafa Garip
 */


#include <cstdint>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/in_process_transport/executor_drive.hpp>
#include <SushiRuntime/in_process_transport/in_process_transport.hpp>
#include <SushiRuntime/in_process_transport/thread_drive.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>

#include "support/transport_contract.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using namespace TestSupport;

namespace
{
    /** @brief Seed of the executor the threadless endpoints of a small case share. */
    constexpr std::uint64_t K_EXECUTOR_SEED = 7;

    /**
     * @brief Builds in-process endpoints on one fabric.
     * @tparam IsThreaded True gives each endpoint a ThreadDrive; false gives them ExecutorDrives
     *         over one SeededExecutor and a ManualClock, so an await steps the executor.
     */
    template <bool IsThreaded>
    class InProcessRig final
    {
        public:
            /** @brief Returns the name the typed suite gives this rig. */
            static std::string name()
            {
                return IsThreaded ? "InProcessThreads" : "InProcessExecutor";
            }

            /** @brief Makes the master endpoint, not started. */
            std::shared_ptr<Transport> make_master()
            {
                return InProcessTransport::make_master(fabric_, master_options, make_drive());
            }

            /** @brief Makes a worker endpoint for the master's fabric, not started. */
            std::shared_ptr<Transport> make_worker(bool reconnect)
            {
                InProcessWorkerOptions options;
                options.reconnect = reconnect;
                return InProcessTransport::make_worker(fabric_, options, make_drive());
            }

            /**
             * @brief Waits until a recorder holds @p count events of one kind.
             *
             * A threadless rig runs every posted round first and answers from what was recorded.
             */
            [[nodiscard]] bool await(EventRecorder& recorder, EventKind kind, std::size_t count)
            {
                if constexpr (IsThreaded)
                {
                    return recorder.wait_for_count(kind, count);
                }
                else
                {
                    executor_->run_all();
                    return recorder.has_count(kind, count);
                }
            }

            /** @brief The settings the next master is made with. */
            InProcessMasterOptions master_options;

        private:
            /** @brief Returns a drive over the rig's executor and clock, or a thread. */
            [[nodiscard]] std::unique_ptr<TransportDrive> make_drive() const
            {
                if constexpr (IsThreaded)
                    return std::make_unique<ThreadDrive>();
                else
                    return std::make_unique<ExecutorDrive>(executor_, clock_);
            }

            /// @brief Runs the pump rounds of a threadless rig.
            std::shared_ptr<SeededExecutor> executor_ =
                std::make_shared<SeededExecutor>(K_EXECUTOR_SEED);

            /// @brief Times the endpoints of a threadless rig; no case advances it.
            std::shared_ptr<const Clock> clock_ = std::make_shared<ManualClock>();

            /// @brief The fabric both endpoints attach to.
            InProcessFabric fabric_;
    };

    using ExecutorRigs = ::testing::Types<InProcessRig<false>>;
    using ThreadRigs = ::testing::Types<InProcessRig<true>>;
} // namespace

INSTANTIATE_TYPED_TEST_SUITE_P(Small_InProcessTransport, TransportContract, ExecutorRigs,
                               RigNames);
INSTANTIATE_TYPED_TEST_SUITE_P(Medium_InProcessTransport, ThreadedTransportContract, ThreadRigs,
                               RigNames);

/** @brief Runs the options case of an in-process master on threadless endpoints. */
class Small_InProcessTransportOptions : public TransportContract<InProcessRig<false>>
{
};

/** @brief Checks that a master whose control queue holds no frame refuses a send to a connected worker. */
TEST_F(Small_InProcessTransportOptions, AControlQueueOfNoFramesRefusesEverySend)
{
    rig_.master_options.control_queue_frames = 0;
    connect(false);

    EXPECT_EQ(master_->send(worker_peer_, MessageType::Ping, {}), SendStatus::QueueFull);
}
