/**************************************************************************/
/* message_router.hpp                                                     */
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
 * @file message_router.hpp
 * @brief Declares MessageRouter, the table from a message type to the handler of its frames.
 * @author Mustafa Garip
 * @see modules/extension/wire/README.md
 */

#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

#include <SushiRuntime/transport/message_type.hpp>

namespace SushiRuntime::Distributed::Wire
{
    /**
     * @brief Names what a router did with a frame.
     */
    enum class RouteStatus : std::uint8_t
    {
        Delivered, /**< The handler of the type ran. */
        Unhandled, /**< No handler is set for the type, or the type names no message. */
    };

    /**
     * @brief Maps each message type to one handler and calls it by array index.
     *
     * Handlers are set before the first frame arrives; route() is const, so reader threads
     * may then call it at once.
     *
     * @tparam Arguments What a handler receives: the peer, the header, the payload.
     */
    template <typename... Arguments>
    class MessageRouter final
    {
        public:
            /** @brief Names the callable that receives the frames of one type. */
            using Handler = std::function<void(Arguments...)>;

            /**
             * @brief Sets the handler of a type, replacing the one set before.
             * @pre type is an enumerator of MessageType.
             */
            void set_handler(MessageType type, Handler handler)
            {
                const std::size_t slot = slot_of(type);
                assert(slot != 0);
                if (slot != 0)
                    handlers_[slot] = std::move(handler);
            }

            /** @brief Returns whether a handler is set for a type. */
            [[nodiscard]] bool has_handler(MessageType type) const noexcept
            {
                return static_cast<bool>(handlers_[slot_of(type)]);
            }

            /** @brief Calls the handler of a type with the given arguments. */
            [[nodiscard]] RouteStatus route(MessageType type, Arguments... arguments) const
            {
                const Handler& handler = handlers_[slot_of(type)];
                if (!handler)
                    return RouteStatus::Unhandled;
                handler(std::forward<Arguments>(arguments)...);
                return RouteStatus::Delivered;
            }

        private:
            /** @brief Returns the slot of a type; slot 0 stays empty and takes every bad type. */
            [[nodiscard]] static constexpr std::size_t slot_of(MessageType type) noexcept
            {
                const std::uint8_t value = static_cast<std::uint8_t>(type);
                return is_message_type(value) ? value : 0;
            }

            /// @brief Handlers indexed by type byte.
            std::array<Handler, K_MESSAGE_TYPE_COUNT + 1> handlers_;
    };
} // namespace SushiRuntime::Distributed::Wire
