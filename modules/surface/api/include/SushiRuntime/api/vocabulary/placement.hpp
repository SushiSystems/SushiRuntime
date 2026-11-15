/**************************************************************************/
/* placement.hpp                                                          */
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
 * @file placement.hpp
 * @brief Declares On, the device an untracked kernel is placed on.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
 */

#pragma once

#include <optional>
#include <string>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/api/runtime/placement_scope.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/region/liveness.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Returns the device a handle states, or a refusal when it states none.
         * @param what Names the handle in the message: "buffer" or "state".
         * @param device The handle's device_index().
         * @param refusal Receives the InvalidArgument error when device is empty.
         * @return The device, or device 0 as a placeholder that the refusal overrides.
         */
        inline DeviceIndex stated_device(const char* what, std::optional<DeviceIndex> device,
                                         std::optional<Error>& refusal)
        {
            if (device)
                return *device;
            refusal = Error{ErrorCode::InvalidArgument,
                            std::string("On(") + what + "): the " + what + " states no device, "
                            "because it is planned or empty; place the kernel with "
                            "On(DeviceIndex) or On(placement_scope)."};
            return DeviceIndex{0};
        }
    } // namespace Detail

    /**
     * @brief States the device a kernel that names no region runs on.
     * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
     */
    struct On
    {
        /**
         * @brief Why the placement is refused when a node uses it, or empty.
         *
         * Declared first, so a handle's constructor writes it before it is built over.
         */
        std::optional<Error> refusal;

        DeviceIndex device;          /**< Device the kernel runs on. */
        LivenessObserver life; /**< Observer of the handle named, or empty. */

        /** @brief The scope whose rules choose the device when the recorder reads it, or none. */
        std::optional<PlacementScope> scope;

        /**
         * @brief Places the kernel on a device by index.
         * @param index Position in the runtime's device list.
         */
        explicit On(DeviceIndex index) noexcept : device(index)
        {

        }

        /**
         * @brief Places the kernel on the device the rules in force give a scope.
         *
         * The recorder reads the device when the verb is recorded. A refusal of the rules goes to
         * Recorder::refusal() and the node is not recorded.
         * @param placement_scope Scope from Runtime::placement_scope().
         */
        explicit On(const PlacementScope& placement_scope) noexcept
            : device(DeviceIndex{0}), scope(placement_scope)
        {

        }

        /**
         * @brief Places the kernel on the device that owns a buffer, and watches the buffer.
         * @param buffer Handle whose device is taken. Alive when the graph runs;
         *          free to move after add().
         * @note A planned or empty buffer states no device; the recorder refuses the node with
         *       InvalidArgument.
         */
        template <typename T>
        explicit On(const Buffer<T>& buffer)
            : device(Detail::stated_device("buffer", buffer.device_index(), refusal)),
              life(Detail::HandleAccess::liveness(buffer))
        {

        }

        /**
         * @brief Places the kernel on the device that owns a State, and watches the State.
         * @param state Handle whose device is taken. Alive when the graph runs;
         *          free to move after add().
         * @note A planned or empty state states no device; the recorder refuses the node with
         *       InvalidArgument.
         */
        template <typename T>
        explicit On(const State<T>& state)
            : device(Detail::stated_device("state", state.device_index(), refusal)),
              life(Detail::HandleAccess::liveness(state))
        {

        }
    };



} // namespace SushiRuntime
