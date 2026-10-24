/**************************************************************************/
/* held_nodes.cpp                                                         */
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
 * @file held_nodes.cpp
 * @brief Implements HeldNodes: the hold, the release and the first offer of a waiting node.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#include <SushiRuntime/offload_coordination/held_nodes.hpp>

#include <algorithm>

namespace SushiRuntime
{
    namespace Distributed
    {
        std::chrono::steady_clock::time_point
        HeldNodes::first_offer(const Internal::Graph::Node& node,
                               std::chrono::steady_clock::time_point now)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return first_offers_.try_emplace(&node, now).first->second;
        }

        void HeldNodes::forget(const Internal::Graph::Node& node)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            first_offers_.erase(&node);
        }

        std::uint64_t HeldNodes::releases() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return releases_;
        }

        HoldStatus HeldNodes::hold(Internal::Graph::Node& node,
                                   std::chrono::steady_clock::time_point until,
                                   std::uint64_t seen)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (closed_)
                return HoldStatus::Closed;
            if (releases_ != seen)
                return HoldStatus::Stale;

            entries_.push_back(Entry{&node, until});
            // A hold only adds an entry, so the earliest deadline is the old one or this one.
            const auto held_until = until.time_since_epoch().count();
            if (held_until < earliest_.load(std::memory_order_relaxed))
                earliest_.store(held_until, std::memory_order_release);
            return HoldStatus::Held;
        }

        void HeldNodes::take_all(std::vector<Internal::Graph::Node*>& out)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            // The reserve throws before the table changes, so a node leaves once or not at all.
            out.reserve(out.size() + entries_.size());
            for (const Entry& entry : entries_)
                out.push_back(entry.node);
            entries_.clear();
            ++releases_;
            publish_earliest();
        }

        void HeldNodes::take_expired(std::chrono::steady_clock::time_point now,
                                     std::vector<Internal::Graph::Node*>& out)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto expired = std::stable_partition(
                entries_.begin(), entries_.end(),
                [now](const Entry& entry) { return entry.until > now; });
            out.reserve(out.size() + static_cast<std::size_t>(entries_.end() - expired));
            for (auto position = expired; position != entries_.end(); ++position)
                out.push_back(position->node);
            entries_.erase(expired, entries_.end());
            publish_earliest();
        }

        std::chrono::steady_clock::time_point HeldNodes::earliest() const noexcept
        {
            return std::chrono::steady_clock::time_point(
                std::chrono::steady_clock::duration(earliest_.load(std::memory_order_acquire)));
        }

        void HeldNodes::close() noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }

        void HeldNodes::drain(std::vector<Internal::Graph::Node*>& out)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            // The reserve throws before the table changes, so a node leaves once or not at all.
            out.reserve(out.size() + entries_.size());
            for (const Entry& entry : entries_)
                out.push_back(entry.node);
            entries_.clear();
            first_offers_.clear();
            publish_earliest();
        }

        std::size_t HeldNodes::size() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return entries_.size();
        }

        void HeldNodes::publish_earliest() noexcept
        {
            std::chrono::steady_clock::time_point earliest =
                std::chrono::steady_clock::time_point::max();
            for (const Entry& entry : entries_)
                earliest = std::min(earliest, entry.until);
            earliest_.store(earliest.time_since_epoch().count(), std::memory_order_release);
        }

    } // namespace Distributed
} // namespace SushiRuntime
