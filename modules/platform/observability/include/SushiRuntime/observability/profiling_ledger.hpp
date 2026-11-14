/**************************************************************************/
/* profiling_ledger.hpp                                                   */
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
 * @file profiling_ledger.hpp
 * @brief Declares ProfilingLedger, the per-plan store of node event records.
 * @author Mustafa Garip
 * @see modules/platform/observability/README.md
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include <SushiRuntime/report/clock_pairing.hpp>
#include <SushiRuntime/report/dispatch_width.hpp>
#include <SushiRuntime/report/drop_counts.hpp>
#include <SushiRuntime/report/node_event_record.hpp>
#include <SushiRuntime/observability/offset_bounds.hpp>

namespace SushiRuntime
{
    namespace Internal::Observability
    {
        /** @brief Names the node and the circumstances of one dispatch, for ProfilingLedger::record(). */
        struct DispatchFacts
        {
            std::uint32_t node_index = 0;                    /**< Registration index of the node. */
            std::uint32_t invocation = 0;                    /**< Step index of the dispatch within the run. */
            std::uint32_t launched_width = K_WIDTH_UNKNOWN;  /**< Elements the dispatch launched over. */
            std::uint32_t live_width = K_WIDTH_UNKNOWN;      /**< Elements that did work, when known. */
            std::uint32_t device_index = K_HOST_DEVICE_INDEX;/**< Device whose queue ran it, or K_HOST_DEVICE_INDEX. */
            std::uint32_t submit_thread = 0;                 /**< Profiler thread index of the submitter; 0 when unknown. */
        };

        /** @brief Holds the three timestamps of one dispatch: the submit on the steady clock, the run on its device's clock. */
        struct DispatchTimes
        {
            std::uint64_t submit_ns = 0; /**< Steady reading taken before the host began to submit the dispatch. */
            std::uint64_t start_ns = 0;  /**< When it started, on its device's event clock or the steady clock for the host. */
            std::uint64_t end_ns = 0;    /**< When it ended, on the same clock as start_ns. */
        };

        /**
         * @brief Stores one run's node event records per plan, at a high-water-mark size.
         * @see modules/platform/observability/README.md
         */
        class ProfilingLedger
        {
        public:
            /** @brief Sentinel returned by claim_slot() when a node's row is full. */
            static constexpr std::uint32_t npos = static_cast<std::uint32_t>(-1);

            ProfilingLedger() = default;

            /** @brief Move-only; hand-written since std::atomic has no move constructor. */
            ProfilingLedger(ProfilingLedger&& other) noexcept
            {
                take(other);
            }

            /** @brief See the move constructor; every atomic is transferred by value. */
            ProfilingLedger& operator=(ProfilingLedger&& other) noexcept
            {
                if (this != &other)
                    take(other);
                return *this;
            }

            ProfilingLedger(const ProfilingLedger&) = delete;
            ProfilingLedger& operator=(const ProfilingLedger&) = delete;

            /**
             * @brief Grows the ledger for a new run and resets its per-run state.
             * @param multiplicities Internal nodes per registration index; its size is the row count.
             * @param expected_invocations Dispatches per internal node the run is expected to make.
             * @param device_count Devices whose clock pairings the run may narrow.
             * @throws std::bad_alloc When growing the rows or their storage fails.
             */
            void reserve(const std::vector<std::uint32_t>& multiplicities,
                         std::uint32_t expected_invocations, std::size_t device_count)
            {
                const std::size_t node_count = multiplicities.size();
                bool must_rebuild = node_count != node_count_;
                if (!must_rebuild)
                {
                    for (std::size_t i = 0; i < node_count; ++i)
                    {
                        const std::uint32_t needed =
                            std::max<std::uint32_t>(expected_invocations * multiplicities[i],
                                                    row_high_water_[i]);
                        if (needed > row_capacity_[i]) { must_rebuild = true; break; }
                    }
                }

                if (must_rebuild)
                {
                    node_count_ = node_count;
                    row_capacity_.assign(node_count_, 0);
                    row_offset_.assign(node_count_, 0);
                    if (row_high_water_.size() != node_count_)
                        row_high_water_.assign(node_count_, 0);

                    std::size_t total = 0;
                    for (std::size_t i = 0; i < node_count_; ++i)
                    {
                        const std::uint32_t needed =
                            std::max<std::uint32_t>(expected_invocations * multiplicities[i],
                                                    row_high_water_[i]);
                        row_capacity_[i] = needed;
                        row_offset_[i] = total;
                        total += needed;
                    }
                    records_.assign(total, NodeEventRecord{});
                    cursors_ = std::make_unique<std::atomic<std::uint32_t>[]>(
                        node_count_ ? node_count_ : 1);
                }

                if (device_count != device_count_)
                {
                    device_count_ = device_count;
                    pairings_ = std::make_unique<DeviceBounds[]>(device_count_ ? device_count_ : 1);
                }

                reset_cursors();
                reset_pairings();
                reset_drops();
            }

            /** @brief Zeroes every node's write cursor without touching capacity. */
            void reset_cursors() noexcept
            {
                for (std::size_t i = 0; i < node_count_; ++i)
                    cursors_[i].store(0, std::memory_order_relaxed);
            }

            /** @brief Claims the next write slot for a node's dispatch, or npos when its row is full. */
            std::uint32_t claim_slot(std::uint32_t node_index) noexcept
            {
                if (node_index >= node_count_) return npos;
                const std::uint32_t slot = cursors_[node_index].fetch_add(1, std::memory_order_relaxed);
                return slot < row_capacity_[node_index] ? slot : npos;
            }

            /** @brief Writes one record into a slot claim_slot() returned for the same node. */
            void write(std::uint32_t node_index, std::uint32_t slot, const NodeEventRecord& record) noexcept
            {
                records_[row_offset_[node_index] + slot] = record;
            }

            /**
             * @brief Fills a whole record from @p facts and @p times and stores it; the one writer of a record.
             *
             * A dispatch that finds no slot is counted as a drop and not stored.
             * @return Whether the record was stored.
             */
            bool record(const DispatchFacts& facts, const DispatchTimes& times) noexcept
            {
                NodeEventRecord event;
                event.node_index = facts.node_index;
                event.invocation = facts.invocation;
                event.launched_width = facts.launched_width;
                event.live_width = facts.live_width;
                event.submit_ns = times.submit_ns;
                event.start_ns = times.start_ns;
                event.end_ns = times.end_ns;
                event.device_index = facts.device_index;
                event.submit_thread = facts.submit_thread;

                const std::uint32_t slot = claim_slot(event.node_index);
                if (slot == npos)
                {
                    note_drop(event.node_index >= node_count_ ? DropCause::UnknownNode
                                                               : DropCause::FullRow,
                              event.node_index);
                    return false;
                }
                write(event.node_index, slot, event);
                return true;
            }

            /** @brief Counts one dispatch of @p node_index that left no record, for @p cause. */
            void note_drop(DropCause cause, std::uint32_t node_index) noexcept
            {
                std::uint32_t expected = DropCounts::K_NO_NODE;
                first_drop_node_.compare_exchange_strong(expected, node_index,
                                                         std::memory_order_relaxed);
                drop_counts_[static_cast<std::size_t>(cause)].fetch_add(1, std::memory_order_relaxed);
            }

            /** @brief Returns the dispatches this run lost so far, per cause. */
            [[nodiscard]] DropCounts drops() const noexcept
            {
                DropCounts counts;
                for (std::size_t cause = 0; cause < DropCounts::K_CAUSES; ++cause)
                    counts.per_cause[cause] = drop_counts_[cause].load(std::memory_order_relaxed);
                counts.first_node = first_drop_node_.load(std::memory_order_relaxed);
                return counts;
            }

            /**
             * @brief Appends every slot a node actually used, in node order, to @p out.
             *
             * Each row's uncapped demand is stored in row_high_water_, and the next
             * reserve() sizes that row to at least this demand. Slots past this run's
             * capacity are not appended; they were counted as FullRow when they were refused.
             */
            void collect(std::vector<NodeEventRecord>& out) noexcept
            {
                for (std::size_t n = 0; n < node_count_; ++n)
                {
                    const std::uint32_t demand = cursors_[n].load(std::memory_order_relaxed);
                    row_high_water_[n] = demand;
                    const std::uint32_t used = std::min(demand, row_capacity_[n]);
                    const std::size_t base = row_offset_[n];
                    for (std::uint32_t i = 0; i < used; ++i)
                        out.push_back(records_[base + i]);
                }
            }

            /**
             * @brief Narrows @p device's clock pairing by what the host saw of one of its dispatches.
             *
             * Every dispatch's bounds hold on their own, so the run keeps the narrowest range they all allow.
             * Workers that reap at once fold their bounds without a lock. A device past the ledger is ignored.
             */
            void narrow_pairing(std::uint32_t device, const DispatchWindow& window) noexcept
            {
                if (device >= device_count_)
                    return;
                DeviceBounds& bounds = pairings_[device];
                const OffsetBounds dispatch = bounds_of(window);
                raise_to(bounds.low_ns, dispatch.low_ns);
                lower_to(bounds.high_ns, dispatch.high_ns);
                lower_to(bounds.reference_event_ns, window.start_ns);
                lower_to(bounds.first_submit_steady_ns, window.submit_steady_ns);
                raise_to(bounds.last_reap_steady_ns, window.reap_steady_ns);
                bounds.has_bounds.store(true, std::memory_order_release);
            }

            /**
             * @brief Appends one DeviceClock for every device that a dispatch narrowed this run.
             *
             * The pairing converts with the midpoint of the device's bounds. Its uncertainty is half their
             * width plus the drift the clocks can make between the device's first submit and last reap
             * (pairing_from(), drift_over()). A device that ran no kernel, or whose events could not be
             * read, has no entry.
             */
            void collect_pairings(std::vector<DeviceClock>& out) const
            {
                for (std::size_t device = 0; device < device_count_; ++device)
                {
                    const DeviceBounds& bounds = pairings_[device];
                    if (!bounds.has_bounds.load(std::memory_order_acquire))
                        continue;
                    const OffsetBounds kept{bounds.low_ns.load(std::memory_order_relaxed),
                                            bounds.high_ns.load(std::memory_order_relaxed)};
                    const std::uint64_t first_submit = bounds.first_submit_steady_ns.load(std::memory_order_relaxed);
                    const std::uint64_t last_reap = bounds.last_reap_steady_ns.load(std::memory_order_relaxed);
                    const std::uint64_t span = last_reap > first_submit ? last_reap - first_submit : 0;
                    DeviceClock clock;
                    clock.device_index = static_cast<std::uint32_t>(device);
                    clock.pairing = pairing_from(kept, bounds.reference_event_ns.load(std::memory_order_relaxed),
                                                 drift_over(span));
                    out.push_back(clock);
                }
            }

        private:
            /** @brief Earliest reading of a device that no dispatch narrowed yet. */
            static constexpr std::uint64_t K_NO_EVENT = static_cast<std::uint64_t>(-1);

            /** @brief The bounds a run's dispatches placed on one device's offset. */
            struct DeviceBounds
            {
                std::atomic<std::int64_t> low_ns{OffsetBounds{}.low_ns};   /**< Largest low bound seen. */
                std::atomic<std::int64_t> high_ns{OffsetBounds{}.high_ns}; /**< Smallest high bound seen. */
                std::atomic<std::uint64_t> reference_event_ns{K_NO_EVENT}; /**< Earliest start seen, on the event clock. */
                std::atomic<std::uint64_t> first_submit_steady_ns{K_NO_EVENT}; /**< Earliest submit seen. */
                std::atomic<std::uint64_t> last_reap_steady_ns{0};             /**< Latest reap seen. */
                std::atomic<bool> has_bounds{false};                       /**< Whether a dispatch narrowed the bounds. */
            };

            /** @brief Raises @p value to @p candidate when the candidate is larger. */
            template <typename Value>
            static void raise_to(std::atomic<Value>& value, Value candidate) noexcept
            {
                Value seen = value.load(std::memory_order_relaxed);
                while (candidate > seen && !value.compare_exchange_weak(seen, candidate, std::memory_order_relaxed))
                {

                }
            }

            /** @brief Lowers @p value to @p candidate when the candidate is smaller. */
            template <typename Value>
            static void lower_to(std::atomic<Value>& value, Value candidate) noexcept
            {
                Value seen = value.load(std::memory_order_relaxed);
                while (candidate < seen && !value.compare_exchange_weak(seen, candidate, std::memory_order_relaxed))
                {

                }
            }

            /** @brief Widens every device's bounds to the whole range again. */
            void reset_pairings() noexcept
            {
                for (std::size_t i = 0; i < device_count_; ++i)
                {
                    pairings_[i].low_ns.store(OffsetBounds{}.low_ns, std::memory_order_relaxed);
                    pairings_[i].high_ns.store(OffsetBounds{}.high_ns, std::memory_order_relaxed);
                    pairings_[i].reference_event_ns.store(K_NO_EVENT, std::memory_order_relaxed);
                    pairings_[i].first_submit_steady_ns.store(K_NO_EVENT, std::memory_order_relaxed);
                    pairings_[i].last_reap_steady_ns.store(0, std::memory_order_relaxed);
                    pairings_[i].has_bounds.store(false, std::memory_order_relaxed);
                }
            }

            /** @brief Zeroes the drop counts. */
            void reset_drops() noexcept
            {
                for (auto& count : drop_counts_)
                    count.store(0, std::memory_order_relaxed);
                first_drop_node_.store(DropCounts::K_NO_NODE, std::memory_order_relaxed);
            }

            /** @brief Takes @p other's storage and leaves it as a default-constructed ledger. */
            void take(ProfilingLedger& other) noexcept
            {
                node_count_ = std::exchange(other.node_count_, 0);
                device_count_ = std::exchange(other.device_count_, 0);
                row_capacity_ = std::move(other.row_capacity_);
                row_offset_ = std::move(other.row_offset_);
                row_high_water_ = std::move(other.row_high_water_);
                records_ = std::move(other.records_);
                cursors_ = std::move(other.cursors_);
                pairings_ = std::move(other.pairings_);
                other.row_capacity_.clear();
                other.row_offset_.clear();
                other.row_high_water_.clear();
                other.records_.clear();
                reset_drops();
                other.reset_drops();
            }

            std::size_t node_count_ = 0;                   /**< Rows the ledger currently indexes. */
            std::size_t device_count_ = 0;                 /**< Devices whose pairings the ledger holds. */
            std::vector<std::uint32_t> row_capacity_;       /**< Slots guaranteed for each row. */
            std::vector<std::size_t> row_offset_;           /**< Each row's base offset into records_. */
            std::vector<std::uint32_t> row_high_water_;     /**< True demand (uncapped) seen per row at the last collect(). */
            std::vector<NodeEventRecord> records_;          /**< Flat, ragged node rows back to back. */
            std::unique_ptr<std::atomic<std::uint32_t>[]> cursors_; /**< Per-node next-slot cursor. */
            std::unique_ptr<DeviceBounds[]> pairings_;      /**< The offset bounds of each device, by device index. */
            std::array<std::atomic<std::uint32_t>, DropCounts::K_CAUSES> drop_counts_{}; /**< Dispatches lost per DropCause. */
            std::atomic<std::uint32_t> first_drop_node_{DropCounts::K_NO_NODE}; /**< Node of the first dispatch lost. */
        };

    } // namespace Observability
} // namespace SushiRuntime
