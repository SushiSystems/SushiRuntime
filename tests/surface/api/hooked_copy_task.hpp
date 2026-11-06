/**************************************************************************/
/* hooked_copy_task.hpp                                                   */
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
 * @file hooked_copy_task.hpp
 * @brief Declares HookedCopyTask, a host task whose next copy runs a hook the test arms.
 * @see tests/surface/api/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <functional>
#include <memory>
#include <utility>

namespace SushiRuntime
{
    namespace Testing
    {
        /**
         * @brief Does nothing when it runs; the first copy made after the test arms the hook
         *        calls it once and disarms it.
         * @see tests/surface/api/README.md
         */
        class HookedCopyTask final
        {
            public:
                /**
                 * @brief Builds a task that shares a hook slot with the test.
                 * @param hook Empty while disarmed; shared with the test, which arms it.
                 */
                explicit HookedCopyTask(std::shared_ptr<std::function<void()>> hook) noexcept
                    : hook_(std::move(hook))
                {
                }

                /** @brief Copies other and runs the armed hook once, disarming it first. */
                HookedCopyTask(const HookedCopyTask& other) : hook_(other.hook_)
                {
                    if (!*hook_)
                        return;
                    const std::function<void()> armed = std::move(*hook_);
                    *hook_ = nullptr;
                    armed();
                }

                HookedCopyTask(HookedCopyTask&&) noexcept = default;
                HookedCopyTask& operator=(const HookedCopyTask&) = delete;
                HookedCopyTask& operator=(HookedCopyTask&&) = delete;
                ~HookedCopyTask() = default;

                /** @brief Does nothing. */
                void operator()() const noexcept
                {

                }

            private:
                /** @brief The hook slot shared with the test; empty while disarmed. */
                std::shared_ptr<std::function<void()>> hook_;
        };
    } // namespace Testing
} // namespace SushiRuntime
