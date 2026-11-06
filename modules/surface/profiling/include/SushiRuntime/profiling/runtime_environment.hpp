/**************************************************************************/
/* runtime_environment.hpp                                                */
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
 * @file runtime_environment.hpp
 * @brief Declares RuntimeEnvironment, the contributor that states the runtime's machine and build as facts.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <memory>
#include <vector>

#include <SushiProfiler/environment/environment_contributor.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        class RuntimeContext;
    } // namespace Execution

    namespace Internal::Profiling
    {
        /**
         * @brief Sets the facts of the runtimes it was given: version, machine, devices with their
         *        drivers, workers, and the build of this library.
         *
         * Compiled inside the runtime library, so the `build` facts are the library's own and not the
         * host's. The facts of a runtime are keyed by its node name, `sushiruntime.<node>.`.
         * @see modules/surface/profiling/README.md
         */
        class RuntimeEnvironment final : public SushiProfiler::EnvironmentContributor
        {
            public:
                /** @brief Builds a contributor over @p runtimes, which outlives it and may grow between sessions. */
                explicit RuntimeEnvironment(
                    const std::vector<std::shared_ptr<Execution::RuntimeContext>>& runtimes) noexcept;

                /** @brief Sets the facts of every runtime in @p facts. */
                void contribute(SushiProfiler::RecordFacts& facts) const override;

            private:
                /** @brief The runtimes whose facts are stated. */
                const std::vector<std::shared_ptr<Execution::RuntimeContext>>& runtimes_;
        };
    } // namespace Profiling
} // namespace SushiRuntime
