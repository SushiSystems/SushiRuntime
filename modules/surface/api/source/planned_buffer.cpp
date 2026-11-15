/**************************************************************************/
/* planned_buffer.cpp                                                     */
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
 * @file planned_buffer.cpp
 * @brief Implements the calls a Residency::Planned Buffer makes into the runtime.
 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/api/data/buffer.hpp>

#include <utility>

#include <SushiRuntime/coherence/planned_host_access.hpp>
#include <SushiRuntime/coherence/usm_instance_allocator.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/task/handle_location.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        namespace
        {
            /**
             * @brief Runs action with a Internal::Coherence::PlannedHostAccess over the runtime of context.
             * @return What action returns, or an AllocationFailed error when the access cannot
             *         be built.
             */
            template <typename Action>
            auto with_access(const ContextHandle& context, Action&& action) -> decltype(action(
                std::declval<Internal::Coherence::PlannedHostAccess&>()))
            {
                Internal::Execution::RuntimeContext& runtime = Internal::ContextAccess::context(context);
                Internal::Coherence::USMInstanceAllocator allocator(runtime.shared_memory());
                Internal::Coherence::PlannedHostAccess access(runtime.handles(), runtime.memory_spaces(),
                                         runtime.transfer_paths(), allocator,
                                         Internal::Graph::host_memory_space());
                return action(access);
            }
        } // namespace

        Result<GlobalHandle> mint_planned_handle(const ContextHandle& context, HandleKind kind,
                                                 std::size_t bytes, std::size_t element_size,
                                                 std::size_t element_alignment)
        {
            if (!context)
                return GlobalHandle::null();
            return with_access(context, [&](Internal::Coherence::PlannedHostAccess& access)
            {
                return access.mint(kind, bytes,
                                   Internal::Memory::ElementLayout{element_size, element_alignment});
            });
        }

        Result<void> read_planned(const ContextHandle& context, GlobalHandle handle,
                                  std::size_t begin, void* destination, std::size_t bytes) noexcept
        {
            Result<void> outcome;
            Result<void> built = translate_exceptions([&]()
            {
                outcome = with_access(context, [&](Internal::Coherence::PlannedHostAccess& access)
                {
                    return access.read(handle, begin, destination, bytes);
                });
            });
            return built ? std::move(outcome) : std::move(built);
        }

        Result<void> write_planned(const ContextHandle& context, GlobalHandle handle,
                                   std::size_t begin, const void* source, std::size_t bytes) noexcept
        {
            Result<void> outcome;
            Result<void> built = translate_exceptions([&]()
            {
                outcome = with_access(context, [&](Internal::Coherence::PlannedHostAccess& access)
                {
                    return access.write(handle, begin, source, bytes);
                });
            });
            return built ? std::move(outcome) : std::move(built);
        }

        void release_planned_handle(const ContextHandle& context, GlobalHandle handle) noexcept
        {
            if (handle.is_null() || !context)
                return;
            // Building the access allocates nothing, so release cannot throw.
            with_access(context, [handle](Internal::Coherence::PlannedHostAccess& access)
            {
                access.release(handle);
            });
        }
    } // namespace Detail
} // namespace SushiRuntime
