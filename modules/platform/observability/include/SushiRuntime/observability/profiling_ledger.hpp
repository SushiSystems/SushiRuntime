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

        /** @brief Holds the three timestamps of one dispatch, on its device's event clock or the steady clock. */
        struct DispatchTimes
        {
            std::uint64_t submit_ns = 0; /**< When the dispatch was submitted. */
            std::uint64_t start_ns = 0;  /**< When it started. */
            std::uint64_t end_ns = 0;    /**< When it ended. */
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
             * @param device_count Devices whose clock pairings the run may claim.
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
                    pairings_ = std::make_unique<PairingSlot[]>(device_count_ ? device_count_ : 1);
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
             * @brief Claims @p device's clock pairing for this run; true for the winner.
             *
             * The owner is the synchronizing atomic: a winning compare_exchange publishes task
             * with release, and is_pairing_owner() reads it with acquire, so a reaping worker on
             * another thread never observes a half-written owner.
             * @return False when another task holds the claim or @p device is out of range.
             */
            bool claim_pairing_owner(std::uint32_t device, const void* task) noexcept
            {
                if (device >= device_count_)
                    return false;
                const void* expected = nullptr;
                return pairings_[device].owner.compare_exchange_strong(
                    expected, task, std::memory_order_acq_rel, std::memory_order_relaxed);
            }

            /**
             * @brief Gives up @p device's claim when @p task holds it, so a later submission can claim it.
             *
             * A task body that submitted nothing has no event to pair; keeping its claim would leave
             * the device with no pairing for the run.
             */
            void release_pairing_owner(std::uint32_t device, const void* task) noexcept
            {
                if (device >= device_count_)
                    return;
                const void* expected = task;
                pairings_[device].owner.compare_exchange_strong(
                    expected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed);
            }

            /** @brief Whether task is the pairing-owning submission of @p device. */
            bool is_pairing_owner(std::uint32_t device, const void* task) const noexcept
            {
                return device < device_count_ &&
                       pairings_[device].owner.load(std::memory_order_acquire) == task;
            }

            /** @brief Records the steady-clock window the pairing owner's submit call spanned. */
            void set_pairing_submit_window(std::uint32_t device, std::uint64_t steady_ns,
                                           std::uint64_t uncertainty_ns) noexcept
            {
                if (device >= device_count_)
                    return;
                pairings_[device].steady_ns.store(steady_ns, std::memory_order_release);
                pairings_[device].uncertainty_ns.store(uncertainty_ns, std::memory_order_release);
            }

            /** @brief Records the pairing owner's event-clock submit reading, read at reap. */
            void set_pairing_event_ns(std::uint32_t device, std::uint64_t event_ns) noexcept
            {
                if (device >= device_count_)
                    return;
                pairings_[device].event_ns.store(event_ns, std::memory_order_release);
                pairings_[device].has_event.store(true, std::memory_order_release);
            }

            /**
             * @brief Appends one DeviceClock for every device whose pairing owner read its event.
             *
             * A device that ran no kernel, or whose owner's event could not be read, has no entry.
             */
            void collect_pairings(std::vector<DeviceClock>& out) const
            {
                for (std::size_t device = 0; device < device_count_; ++device)
                {
                    const PairingSlot& slot = pairings_[device];
                    if (!slot.has_event.load(std::memory_order_acquire))
                        continue;
                    DeviceClock clock;
                    clock.device_index = static_cast<std::uint32_t>(device);
                    clock.pairing = ClockPairing{slot.event_ns.load(std::memory_order_acquire),
                                                 slot.steady_ns.load(std::memory_order_acquire),
                                                 slot.uncertainty_ns.load(std::memory_order_acquire)};
                    out.push_back(clock);
                }
            }

        private:
            /** @brief One device's claim on the run's clock pairing. */
            struct PairingSlot
            {
                std::atomic<const void*> owner{nullptr};  /**< Task identity that claimed the pairing. */
                std::atomic<std::uint64_t> steady_ns{0};      /**< Steady reading beside the pairing submission. */
                std::atomic<std::uint64_t> uncertainty_ns{0}; /**< Steady spread across the pairing submit call. */
                std::atomic<std::uint64_t> event_ns{0};       /**< Pairing submission's command_submit, at reap. */
                std::atomic<bool> has_event{false};           /**< Whether event_ns was read. */
            };

            /** @brief Clears every device's pairing claim. */
            void reset_pairings() noexcept
            {
                for (std::size_t device = 0; device < device_count_; ++device)
                {
                    pairings_[device].owner.store(nullptr, std::memory_order_relaxed);
                    pairings_[device].steady_ns.store(0, std::memory_order_relaxed);
                    pairings_[device].uncertainty_ns.store(0, std::memory_order_relaxed);
                    pairings_[device].event_ns.store(0, std::memory_order_relaxed);
                    pairings_[device].has_event.store(false, std::memory_order_relaxed);
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
            std::unique_ptr<PairingSlot[]> pairings_;       /**< One pairing claim per device. */
            std::array<std::atomic<std::uint32_t>, DropCounts::K_CAUSES> drop_counts_{}; /**< Dispatches lost per DropCause. */
            std::atomic<std::uint32_t> first_drop_node_{DropCounts::K_NO_NODE}; /**< Node of the first dispatch lost. */
        };

    } // namespace Observability
} // namespace SushiRuntime
