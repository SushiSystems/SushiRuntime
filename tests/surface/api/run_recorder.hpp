/**************************************************************************/
/* run_recorder.hpp                                                       */
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
 * @file run_recorder.hpp
 * @brief Declares RunRecorder, which keeps the observations of a runtime's runs for a test to read.
 * @see tests/surface/api/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/test_runner/recording_observer.hpp>

namespace SushiRuntime
{
    namespace Testing
    {
        /**
         * @brief Adds a recording observer to a runtime and removes it when stopped or destroyed.
         *
         * Adding the observer turns device timing on, so every graph of the runtime records timing
         * from its next run. The helper also converts a device record to the steady clock.
         * @see tests/surface/api/README.md
         */
        class RunRecorder final
        {
            public:
                /** @brief Adds a recording observer to @p runtime. */
                explicit RunRecorder(const Runtime& runtime)
                    : observer_(std::make_shared<TestRunner::RecordingObserver>()),
                      handle_(runtime.add_run_observer(observer_))
                {
                }

                RunRecorder(const RunRecorder&) = delete;
                RunRecorder& operator=(const RunRecorder&) = delete;
                RunRecorder(RunRecorder&&) = delete;
                RunRecorder& operator=(RunRecorder&&) = delete;
                ~RunRecorder() = default;

                /** @brief Removes the observer; later runs are not recorded. */
                void stop() noexcept
                {
                    handle_.reset();
                }

                /** @brief Returns how many runs were observed. */
                [[nodiscard]] std::size_t count() const
                {
                    return observer_->count();
                }

                /**
                 * @brief Returns the latest observation.
                 * @pre count() is not 0.
                 */
                [[nodiscard]] RunObservation last() const
                {
                    return observer_->last();
                }

                /** @brief Returns every observation, oldest first. */
                [[nodiscard]] std::vector<RunObservation> runs() const
                {
                    return observer_->runs();
                }

                /** @brief Returns the pairing of device @p device in @p observation, or null when it has none. */
                [[nodiscard]] static const ClockPairing* pairing_of(const RunObservation& observation,
                                                                    std::uint32_t device) noexcept
                {
                    for (const DeviceClock& clock : observation.device_clocks)
                        if (clock.device_index == device)
                            return &clock.pairing;
                    return nullptr;
                }

                /**
                 * @brief Returns @p time, read on the clock of @p record, on the steady clock.
                 *
                 * A host record is on the steady clock already. A device record goes through its
                 * device's pairing; a device with none returns @p time unchanged.
                 */
                [[nodiscard]] static std::uint64_t steady_ns(const RunObservation& observation,
                                                             const NodeEventRecord& record,
                                                             std::uint64_t time) noexcept
                {
                    if (record.device_index == K_HOST_DEVICE_INDEX)
                        return time;
                    const ClockPairing* pairing = pairing_of(observation, record.device_index);
                    if (pairing == nullptr)
                        return time;
                    return pairing->steady_ns + (time - pairing->event_ns);
                }

            private:
                /** @brief The observer the runtime holds. */
                std::shared_ptr<TestRunner::RecordingObserver> observer_;

                /** @brief Removes the observer from the runtime. */
                RunObserverHandle handle_;
        };
    } // namespace Testing
} // namespace SushiRuntime
