/**************************************************************************/
/* held_nodes.hpp                                                         */
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
 * @file held_nodes.hpp
 * @brief Declares HeldNodes, the nodes that wait for the worker their rule names.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph { struct Node; }

    namespace Distributed
    {
        /**
         * @brief Names what hold() did with a node.
         */
        enum class HoldStatus : std::uint8_t
        {
            Held,   /**< The table owns the node. */
            Stale,  /**< A release ran since the caller's decision; decide again. */
            Closed, /**< close() was called; the caller keeps the node. */
        };

        /**
         * @brief Owns the nodes a rule holds while their target is absent or busy, and the
         *        time each was first offered.
         *
         * A node leaves through take_all(), take_expired() or drain(); whoever receives it owns
         * the scheduling reference and passes it to the sink once. The table owns one mutex,
         * held only inside its members; it calls no other brick and never reads a node.
         */
        class SUSHIRUNTIME_API HeldNodes final
        {
            public:
                /** @brief Builds an open, empty table. */
                HeldNodes() = default;

                HeldNodes(const HeldNodes&) = delete;
                HeldNodes& operator=(const HeldNodes&) = delete;
                HeldNodes(HeldNodes&&) = delete;
                HeldNodes& operator=(HeldNodes&&) = delete;

                /**
                 * @brief Returns the time a node was first offered in its wait, recording
                 *        @p now when none is recorded.
                 */
                [[nodiscard]] std::chrono::steady_clock::time_point
                first_offer(const Internal::Graph::Node& node,
                            std::chrono::steady_clock::time_point now);

                /** @brief Forgets the first offer of a node; called once its rule placed it. */
                void forget(const Internal::Graph::Node& node);

                /** @brief Returns how many times take_all() has run; read before a decision. */
                [[nodiscard]] std::uint64_t releases() const;

                /**
                 * @brief Takes ownership of a node until @p until, unless a take_all() ran
                 *        since the caller read @p seen from releases().
                 *
                 * On Held the caller must not touch the node again; otherwise it keeps it.
                 */
                [[nodiscard]] HoldStatus hold(Internal::Graph::Node& node,
                                              std::chrono::steady_clock::time_point until,
                                              std::uint64_t seen);

                /** @brief Removes every held node, for a re-offer after a worker changed. */
                void take_all(std::vector<Internal::Graph::Node*>& out);

                /** @brief Removes every node whose wait ends at or before @p now. */
                void take_expired(std::chrono::steady_clock::time_point now,
                                  std::vector<Internal::Graph::Node*>& out);

                /**
                 * @brief Returns the earliest end of a held wait, or time_point::max().
                 *
                 * Reads an atomic and takes no lock.
                 */
                [[nodiscard]] std::chrono::steady_clock::time_point earliest() const noexcept;

                /** @brief Refuses every later hold(). */
                void close() noexcept;

                /** @brief Removes every node left and forgets every first offer. */
                void drain(std::vector<Internal::Graph::Node*>& out);

                /** @brief Returns the number of held nodes. */
                [[nodiscard]] std::size_t size() const;

            private:
                /** @brief Holds one node and the end of its wait. */
                struct Entry
                {
                    Internal::Graph::Node* node = nullptr;       /**< The node held. */
                    std::chrono::steady_clock::time_point until; /**< The end of its wait. */
                };

                /** @brief Stores the earliest end of the held waits; called under the mutex. */
                void publish_earliest() noexcept;

                /// @brief Guards every member but earliest_.
                mutable std::mutex mutex_;

                /// @brief Set by close().
                bool closed_ = false;

                /// @brief The number of take_all() calls.
                std::uint64_t releases_ = 0;

                /// @brief The held nodes, in the order they were held.
                std::vector<Entry> entries_;

                /// @brief The first offer of each node in a wait, held or re-offered.
                std::unordered_map<const Internal::Graph::Node*,
                                   std::chrono::steady_clock::time_point> first_offers_;

                /// @brief The earliest end of a held wait, in ticks of the steady clock.
                std::atomic<std::chrono::steady_clock::rep> earliest_{
                    std::chrono::steady_clock::time_point::max().time_since_epoch().count()};
        };

    } // namespace Distributed
} // namespace SushiRuntime
