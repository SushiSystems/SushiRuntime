/**************************************************************************/
/* kernel_pointers.hpp                                                    */
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
 * @file kernel_pointers.hpp
 * @brief Declares KernelPointers, a node's kernel pointers bound on its device, and their scope.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_resolver.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /** @brief Describes one pointer a kernel receives: its buffer and its home instance. */
        struct KernelPointerSlot
        {
            GlobalHandle handle;                    /**< Buffer the pointer points into. */
            void* home = nullptr;                   /**< Address recorded at add(). */
            std::optional<std::size_t> home_device; /**< Device that holds the home instance; none for host or planned memory. */
        };

        /**
         * @brief Holds the pointers one node's kernel receives, rebound to the node's device at
         *        each dispatch.
         *
         * One thread dispatches a node at a time, so bind() takes no lock.
         * @see modules/engine/task/README.md
         */
        class KernelPointers final
        {
            public:
                /**
                 * @brief Builds the table of @p slots, resolved through @p resolver.
                 * @param resolver Outlives the table.
                 */
                SUSHIRUNTIME_API KernelPointers(std::vector<KernelPointerSlot> slots,
                                                const HandleResolver& resolver);

                /**
                 * @brief Points every slot at its buffer's instance on @p device.
                 *
                 * A slot whose buffer has no instance there keeps its home address, which is how
                 * host, borrowed and shared memory is reached.
                 */
                SUSHIRUNTIME_API void bind(std::size_t device) noexcept;

                /** @brief Returns the pointers of the last bind(), one per slot, in slot order. */
                [[nodiscard]] void* const* table() const noexcept { return bound_.data(); }

                /** @brief Returns the number of slots. */
                [[nodiscard]] std::size_t size() const noexcept { return bound_.size(); }

                /** @brief Returns how many bind() calls read no directory, every slot being at home. */
                [[nodiscard]] std::uint64_t home_binds() const noexcept { return home_binds_; }

                /** @brief Returns how many bind() calls resolved at least one slot through the directory. */
                [[nodiscard]] std::uint64_t resolved_binds() const noexcept { return resolved_binds_; }

            private:
                std::vector<KernelPointerSlot> slots_; /**< What each pointer stands for. */
                std::vector<void*> bound_;             /**< The pointers of the last bind(). */
                const HandleResolver* resolver_;       /**< The directory the slots resolve through. */
                std::uint64_t home_binds_ = 0;         /**< Binds that took the home fast path. */
                std::uint64_t resolved_binds_ = 0;     /**< Binds that read the directory. */
        };

        /**
         * @brief Makes a node's bound pointers the ones the kernels built on the calling thread
         *        receive, until the scope ends. Scopes nest.
         * @see modules/engine/task/README.md
         */
        class KernelPointerScope final
        {
            public:
                /** @brief Opens the scope over @p pointers, which outlives it. */
                explicit KernelPointerScope(const KernelPointers& pointers) noexcept
                    : previous_(slot())
                {
                    slot() = &pointers;
                }

                KernelPointerScope(const KernelPointerScope&) = delete;
                KernelPointerScope& operator=(const KernelPointerScope&) = delete;
                KernelPointerScope(KernelPointerScope&&) = delete;
                KernelPointerScope& operator=(KernelPointerScope&&) = delete;

                /** @brief Restores the scope that was open before this one. */
                ~KernelPointerScope()
                {
                    slot() = previous_;
                }

                /** @brief Returns the table of the open scope of this thread, or null when none is open. */
                [[nodiscard]] static const KernelPointers* current() noexcept { return slot(); }

            private:
                /** @brief Returns the slot that holds this thread's open table. */
                SUSHIRUNTIME_API static const KernelPointers*& slot() noexcept;

                /** @brief The table that was open when this scope started. */
                const KernelPointers* previous_;
        };
    } // namespace Internal::Graph
} // namespace SushiRuntime
