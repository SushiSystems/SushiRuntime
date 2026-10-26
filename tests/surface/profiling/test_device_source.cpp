/**************************************************************************/
/* test_device_source.cpp                                                 */
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
 * @file test_device_source.cpp
 * @brief Tests DeviceSource over SushiProfiler's own SourceContext, SpanSink and SpanTable, with no device.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/record/device_span_keys.hpp>
#include <SushiProfiler/record/span_table.hpp>
#include <SushiProfiler/sites/name_table.hpp>
#include <SushiProfiler/sources/source.hpp>
#include <SushiProfiler/sources/source_context.hpp>
#include <SushiProfiler/sources/span_sink.hpp>

#include <SushiRuntime/core/node_cost.hpp>
#include <SushiRuntime/profiling/device_source.hpp>
#include <SushiRuntime/profiling/run_queue.hpp>
#include <SushiRuntime/report/node_table.hpp>
#include <SushiRuntime/report/run_observer.hpp>

namespace Keys = SushiProfiler::DeviceSpanKeys;
using namespace SushiRuntime;
using SushiRuntime::Internal::Profiling::DeviceSource;
using SushiRuntime::Internal::Profiling::FeedAttachment;
using SushiRuntime::Internal::Profiling::RunQueue;
using SushiRuntime::Internal::Profiling::RuntimeTracks;
using SushiProfiler::NameID;

namespace
{
    /** @brief The steady time the record counts from. */
    constexpr std::uint64_t K_ORIGIN = 1000;

    /** @brief Pairs device event time 5000 with steady time 2000, with an uncertainty of 40 ns. */
    constexpr ClockPairing K_PAIRING{5000, 2000, 40};

    /** @brief Records the calls of begin() and end() and can refuse to start. */
    class FakeAttachment final : public FeedAttachment
    {
        public:
            std::string attach() override
            {
                ++attaches;
                return refusal;
            }

            void detach() noexcept override
            {
                ++detaches;
            }

            int attaches = 0;      /**< How often begin() started the delivery. */
            int detaches = 0;      /**< How often end() stopped it. */
            std::string refusal;   /**< The text attach() returns; empty accepts. */
    };

    /** @brief Builds a device record for node @p node on device 0, at event times 5010, 5020, 5120. */
    NodeEventRecord device_record(std::uint32_t node = 0, std::uint32_t device = 0)
    {
        NodeEventRecord record;
        record.node_index = node;
        record.invocation = 2;
        record.device_index = device;
        record.submit_thread = 3;
        record.submit_ns = 5010;
        record.start_ns = 5020;
        record.end_ns = 5120;
        return record;
    }

    /** @brief Builds a table row. */
    NodeTableRow row_of(std::uint32_t index, const char* name, std::uint32_t operation = 0)
    {
        NodeTableRow row;
        row.index = index;
        row.name = name;
        row.operation = OperationID(operation);
        row.multiplicity = 1;
        return row;
    }

    /** @brief Builds an observation of @p events over @p rows with the pairing of device 0. */
    RunObservation observation_of(std::vector<NodeEventRecord> events, std::vector<NodeTableRow> rows,
                                  std::string label = {}, std::uint64_t graph_id = 0)
    {
        RunObservation observation;
        observation.events = std::move(events);
        observation.graph_id = graph_id;
        observation.table = std::make_shared<const NodeTable>(std::move(rows), std::move(label), graph_id);
        observation.device_clocks.push_back(DeviceClock{0, K_PAIRING});
        return observation;
    }

    /** @brief Holds the SushiProfiler objects a source starts on and the source over a queue. */
    class Small_DeviceSource : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                ASSERT_TRUE(source_.begin(context_).ok());
            }

            /** @brief Reads the integer attribute @p key of row @p row, or -1 when the row lacks it. */
            std::int64_t integer_of(std::string_view key, std::size_t row)
            {
                const SushiProfiler::AttributeColumn* column = table_.attribute(names_.intern(key));
                if (column == nullptr || row >= column->present.size() || column->present[row] == 0)
                    return -1;
                return column->integers[row];
            }

            /** @brief Returns whether row @p row has attribute @p key. */
            bool has(std::string_view key, std::size_t row)
            {
                const SushiProfiler::AttributeColumn* column = table_.attribute(names_.intern(key));
                return column != nullptr && row < column->present.size() && column->present[row] != 0;
            }

            /** @brief Collects period [@p start, @p end) in steady nanoseconds. */
            void collect(std::uint64_t start, std::uint64_t end)
            {
                source_.collect(SushiProfiler::PeriodSpan{0, start, end}, sink_);
            }

            /** @brief Pushes @p observation as runtime 0. */
            void push(const RunObservation& observation)
            {
                queue_.push(0, observation);
            }

            SushiProfiler::NameTable names_;                                  /**< The session's names. */
            std::vector<std::string> tracks_;                                 /**< The record's tracks. */
            SushiProfiler::SourceContext context_{names_, tracks_, K_ORIGIN}; /**< What begin() receives. */
            RunQueue queue_{16};                                              /**< The source's queue. */
            FakeAttachment attachment_;                                       /**< Stands for the runtimes' observers. */
            DeviceSource source_{queue_, {RuntimeTracks{"node", {"gpu0", "gpu1"}}}, &attachment_};
            SushiProfiler::SpanTable table_;                                  /**< The period's spans. */
            SushiProfiler::SpanSink sink_{table_, names_, tracks_};           /**< The sink under test. */
    };
} // namespace

/** @brief Tests that event times go through the device's pairing onto the record's clock. */
TEST_F(Small_DeviceSource, ConvertsEventTimesThroughThePairing)
{
    push(observation_of({device_record()}, {row_of(0, "conv")}));

    collect(2000, 3000);

    ASSERT_EQ(table_.size(), 1u);
    const SushiProfiler::Span span = table_.span(0);
    // event 5010 is steady 2010 and record 1010; the origin is 1000.
    EXPECT_EQ(span.queued_ns, 1010u);
    EXPECT_EQ(span.start_ns, 1020u);
    EXPECT_EQ(span.end_ns, 1120u);
    EXPECT_EQ(span.uncertainty_ns, 40u);
    EXPECT_EQ(span.submit_thread, 3u);
    EXPECT_EQ(names_.text(span.name), "conv");
    ASSERT_LT(span.track, tracks_.size());
    EXPECT_EQ(tracks_[span.track], "node/0/gpu0");
    EXPECT_EQ(sink_.refused(), 0u);
}

/** @brief Tests the integer and name attributes every span carries. */
TEST_F(Small_DeviceSource, SetsNodeInvocationOperationAndLayerAttributes)
{
    push(observation_of({device_record(4)}, {row_of(0, "x"), row_of(4, "conv", 0xBEEF)}));

    collect(2000, 3000);

    ASSERT_EQ(table_.size(), 1u);
    EXPECT_EQ(integer_of(Keys::K_NODE, 0), 4);
    EXPECT_EQ(integer_of(Keys::K_INVOCATION, 0), 2);
    EXPECT_EQ(integer_of(Keys::K_OPERATION, 0), 0xBEEF);
    // A name attribute holds the interned id of its value.
    EXPECT_EQ(integer_of("layer", 0), static_cast<std::int64_t>(names_.intern("scheduler")));
}

/** @brief Tests that a chain-layer observation names the chain layer. */
TEST_F(Small_DeviceSource, ANameAttributeNamesTheChainLayer)
{
    RunObservation observation = observation_of({device_record()}, {row_of(0, "conv")});
    observation.layer = ExecutionLayer::Chain;
    push(observation);

    collect(2000, 3000);

    ASSERT_EQ(table_.size(), 1u);
    EXPECT_EQ(integer_of("layer", 0), static_cast<std::int64_t>(names_.intern("chain")));
}

/** @brief Tests that an operation that names none is left out and a width that is unknown is left out. */
TEST_F(Small_DeviceSource, OmitsAWidthThatIsUnknown)
{
    NodeEventRecord known = device_record(0);
    known.launched_width = 128;
    known.live_width = 100;
    NodeEventRecord unknown = device_record(1);
    push(observation_of({known, unknown}, {row_of(0, "a"), row_of(1, "b")}));

    collect(2000, 3000);

    ASSERT_EQ(table_.size(), 2u);
    EXPECT_EQ(integer_of(Keys::K_LAUNCHED_WIDTH, 0), 128);
    EXPECT_EQ(integer_of(Keys::K_LIVE_WIDTH, 0), 100);
    EXPECT_FALSE(has(Keys::K_LAUNCHED_WIDTH, 1));
    EXPECT_FALSE(has(Keys::K_LIVE_WIDTH, 1));
    EXPECT_FALSE(has(Keys::K_OPERATION, 0)) << "an operation of 0 names none";
}

/** @brief Tests that a node that stated a cost writes flops, bytes and its dtype. */
TEST_F(Small_DeviceSource, WritesTheCostOfANodeThatStatesOne)
{
    NodeTableRow row = row_of(0, "gemm");
    row.cost = NodeCost{4096, 512, NodeDType::BF16};
    push(observation_of({device_record(0)}, {row}));

    collect(2000, 3000);

    ASSERT_EQ(table_.size(), 1u);
    EXPECT_EQ(integer_of(Keys::K_FLOPS, 0), 4096);
    EXPECT_EQ(integer_of(Keys::K_BYTES, 0), 512);
    EXPECT_EQ(integer_of(Keys::K_DTYPE, 0), static_cast<std::int64_t>(names_.intern("bf16")));
}

/** @brief Tests that a stated zero cost is written as zeros and an absent cost writes nothing. */
TEST_F(Small_DeviceSource, WritesAStatedZeroCostAndOmitsAnAbsentOne)
{
    NodeTableRow moves = row_of(0, "copy");
    moves.cost = NodeCost{0, 1024, NodeDType::Unstated};
    NodeTableRow none = row_of(1, "plain");
    push(observation_of({device_record(0), device_record(1)}, {moves, none}));

    collect(2000, 3000);

    ASSERT_EQ(table_.size(), 2u);
    EXPECT_TRUE(has(Keys::K_FLOPS, 0));
    EXPECT_EQ(integer_of(Keys::K_FLOPS, 0), 0) << "a data-movement node does no floating-point work";
    EXPECT_EQ(integer_of(Keys::K_BYTES, 0), 1024);
    EXPECT_FALSE(has(Keys::K_DTYPE, 0)) << "no dtype was named";
    EXPECT_FALSE(has(Keys::K_FLOPS, 1));
    EXPECT_FALSE(has(Keys::K_BYTES, 1));
    EXPECT_FALSE(has(Keys::K_DTYPE, 1));
}

/** @brief Tests that a host node goes on the runtime's host track on the steady clock, with no uncertainty. */
TEST_F(Small_DeviceSource, PutsAHostNodeOnTheHostTrack)
{
    NodeEventRecord host;
    host.node_index = 0;
    host.submit_ns = 2500;
    host.start_ns = 2500;
    host.end_ns = 2600;
    push(observation_of({host}, {row_of(0, "native")}));

    collect(2000, 3000);

    ASSERT_EQ(table_.size(), 1u);
    const SushiProfiler::Span span = table_.span(0);
    EXPECT_EQ(tracks_[span.track], "node/host");
    EXPECT_EQ(span.start_ns, 1500u) << "a host time is steady already; only the origin is taken off";
    EXPECT_EQ(span.end_ns, 1600u);
    EXPECT_EQ(span.uncertainty_ns, 0u);
}

/** @brief Tests that a span that starts in a later period waits for it. */
TEST_F(Small_DeviceSource, ASpanOfALaterPeriodWaitsForIt)
{
    NodeEventRecord later = device_record();
    later.submit_ns = later.start_ns = 6500;
    later.end_ns = 6600;
    push(observation_of({later}, {row_of(0, "conv")}));

    collect(2000, 3000);
    EXPECT_EQ(table_.size(), 0u) << "steady 3500 is after the period";
    EXPECT_EQ(sink_.late(), 0u) << "waiting is not dropping";

    SushiProfiler::SpanTable next_table;
    SushiProfiler::SpanSink next_sink(next_table, names_, tracks_);
    source_.collect(SushiProfiler::PeriodSpan{1, 3000, 4000}, next_sink);
    EXPECT_EQ(next_table.size(), 1u) << "the later period takes it";
}

/** @brief Tests that a span that started before the period is dropped late and not added. */
TEST_F(Small_DeviceSource, ASpanOfAClosedPeriodIsDroppedLate)
{
    NodeEventRecord early = device_record();
    early.submit_ns = early.start_ns = 4500;
    early.end_ns = 4600;
    push(observation_of({early}, {row_of(0, "conv")}));

    collect(2000, 3000);

    EXPECT_EQ(table_.size(), 0u);
    EXPECT_EQ(sink_.late(), 1u) << "steady 1500 belongs to a period that has closed";
}

/** @brief Tests that the drops a run carries reach the sink as late spans. */
TEST_F(Small_DeviceSource, ForwardsTheDropsOfARun)
{
    RunObservation observation = observation_of({device_record()}, {row_of(0, "conv")});
    observation.drops.per_cause[static_cast<std::size_t>(DropCause::FullRow)] = 3;
    observation.drops.per_cause[static_cast<std::size_t>(DropCause::UnreadableEvent)] = 1;
    push(observation);

    collect(2000, 3000);

    EXPECT_EQ(table_.size(), 1u);
    EXPECT_EQ(sink_.late(), 4u);
}

/** @brief Tests that a run with no drops forwards none. */
TEST_F(Small_DeviceSource, ARunWithNoDropsForwardsNone)
{
    push(observation_of({device_record()}, {row_of(0, "conv")}));

    collect(2000, 3000);

    EXPECT_EQ(sink_.late(), 0u);
}

/** @brief Tests that a record of a device with no pairing is dropped and counted. */
TEST_F(Small_DeviceSource, AMissingPairingDropsTheSpan)
{
    push(observation_of({device_record(0, 1)}, {row_of(0, "conv")}));

    collect(2000, 3000);

    EXPECT_EQ(table_.size(), 0u) << "device 1 has no pairing, so its span has no start on the steady clock";
    EXPECT_EQ(sink_.late(), 1u);
}

/** @brief Tests that the records a full queue dropped are counted too. */
TEST_F(Small_DeviceSource, TheQueuesOverflowIsCounted)
{
    RunQueue small_queue(1);
    DeviceSource source(small_queue, {RuntimeTracks{"node", {"gpu0"}}});
    ASSERT_TRUE(source.begin(context_).ok());
    small_queue.push(0, observation_of({device_record(), device_record()}, {row_of(0, "a")}));
    small_queue.push(0, observation_of({device_record()}, {row_of(0, "a")}));

    source.collect(SushiProfiler::PeriodSpan{0, 2000, 3000}, sink_);

    EXPECT_EQ(table_.size(), 1u) << "only the newest run is left";
    EXPECT_EQ(sink_.late(), 2u) << "the two records of the dropped run";
    source.end();
}

/** @brief Tests that a record with no table row is named by its index. */
TEST_F(Small_DeviceSource, ARecordWithNoRowIsNamedByItsIndex)
{
    RunObservation observation = observation_of({device_record(7)}, {});
    observation.table = nullptr;
    push(observation);

    collect(2000, 3000);

    ASSERT_EQ(table_.size(), 1u);
    EXPECT_EQ(names_.text(table_.span(0).name), "node 7");
}

/** @brief Tests that an observation of a runtime the source was not given is skipped. */
TEST_F(Small_DeviceSource, AnUnknownRuntimeIsSkipped)
{
    queue_.push(5, observation_of({device_record()}, {row_of(0, "conv")}));

    collect(2000, 3000);

    EXPECT_EQ(table_.size(), 0u);
    EXPECT_EQ(sink_.refused(), 0u);
}

/** @brief Tests that a graph's label is written as the graph attribute of each of its spans. */
TEST_F(Small_DeviceSource, WritesTheGraphLabelWhenOneIsSet)
{
    push(observation_of({device_record(0), device_record(1)}, {row_of(0, "a"), row_of(1, "b")}, "decoder", 7));

    collect(2000, 3000);

    ASSERT_EQ(table_.size(), 2u);
    EXPECT_EQ(integer_of(Keys::K_GRAPH, 0), static_cast<std::int64_t>(names_.intern("decoder")));
    EXPECT_EQ(integer_of(Keys::K_GRAPH, 1), static_cast<std::int64_t>(names_.intern("decoder")));
}

/** @brief Tests that two graphs whose nodes share registration indices are told apart by their labels. */
TEST_F(Small_DeviceSource, TwoGraphsWithEqualNodeIndicesDifferByGraph)
{
    push(observation_of({device_record(0)}, {row_of(0, "x")}, "encoder", 1));
    push(observation_of({device_record(0)}, {row_of(0, "x")}, "decoder", 2));

    collect(2000, 3000);

    ASSERT_EQ(table_.size(), 2u);
    EXPECT_EQ(integer_of(Keys::K_NODE, 0), integer_of(Keys::K_NODE, 1)) << "the same registration index in both";
    EXPECT_NE(integer_of(Keys::K_GRAPH, 0), integer_of(Keys::K_GRAPH, 1));
    EXPECT_EQ(integer_of(Keys::K_GRAPH, 0), static_cast<std::int64_t>(names_.intern("encoder")));
    EXPECT_EQ(integer_of(Keys::K_GRAPH, 1), static_cast<std::int64_t>(names_.intern("decoder")));
}

/** @brief Tests that a node that states an origin has it written as the origin attribute. */
TEST_F(Small_DeviceSource, WritesTheOriginOfANodeThatStatesOne)
{
    NodeTableRow stated = row_of(0, "q_proj");
    stated.origin = "attention";
    push(observation_of({device_record(0), device_record(1)}, {stated, row_of(1, "plain")}));

    collect(2000, 3000);

    ASSERT_EQ(table_.size(), 2u);
    EXPECT_EQ(integer_of(Keys::K_ORIGIN, 0), static_cast<std::int64_t>(names_.intern("attention")));
    EXPECT_FALSE(has(Keys::K_ORIGIN, 1)) << "a node that states none has none";
}

/** @brief Tests that an empty origin and an empty label write no attribute. */
TEST_F(Small_DeviceSource, OmitsTheOriginAndTheLabelWhenEmpty)
{
    push(observation_of({device_record()}, {row_of(0, "conv")}));

    collect(2000, 3000);

    ASSERT_EQ(table_.size(), 1u);
    EXPECT_FALSE(has(Keys::K_GRAPH, 0));
    EXPECT_FALSE(has(Keys::K_ORIGIN, 0));
}

/** @brief Tests that the label of one table is interned once however many runs of it arrive. */
TEST_F(Small_DeviceSource, TheLabelOfATableIsInternedOnceForAllItsRuns)
{
    const RunObservation first = observation_of({device_record()}, {row_of(0, "x")}, "decoder", 1);
    RunObservation second = first; // the same table, as two runs of one plan share it
    push(first);
    push(second);
    collect(2000, 3000);
    const std::size_t names_before = names_.size();

    push(first);
    SushiProfiler::SpanTable next_table;
    SushiProfiler::SpanSink next_sink(next_table, names_, tracks_);
    source_.collect(SushiProfiler::PeriodSpan{1, 2000, 3000}, next_sink);

    ASSERT_EQ(table_.size(), 2u);
    EXPECT_EQ(next_table.size(), 1u);
    EXPECT_EQ(integer_of(Keys::K_GRAPH, 0), integer_of(Keys::K_GRAPH, 1));
    EXPECT_EQ(names_.size(), names_before + 0u) << "the names of the table were interned already";
}

/** @brief Tests that two runtimes get tracks named by their own nodes. */
TEST(Small_DeviceSourceRuntimes, TwoRuntimesGetTracksNamedByTheirNodes)
{
    SushiProfiler::NameTable names;
    std::vector<std::string> tracks;
    SushiProfiler::SourceContext context(names, tracks, K_ORIGIN);
    RunQueue queue(8);
    DeviceSource source(queue, {RuntimeTracks{"alpha", {"gpu"}}, RuntimeTracks{"beta", {"gpu"}}});
    SushiProfiler::SpanTable table;
    SushiProfiler::SpanSink sink(table, names, tracks);
    ASSERT_TRUE(source.begin(context).ok());
    queue.push(0, observation_of({device_record()}, {row_of(0, "a")}));
    queue.push(1, observation_of({device_record()}, {row_of(0, "b")}));

    source.collect(SushiProfiler::PeriodSpan{0, 2000, 3000}, sink);

    ASSERT_EQ(table.size(), 2u);
    EXPECT_EQ(tracks[table.span(0).track], "alpha/0/gpu");
    EXPECT_EQ(tracks[table.span(1).track], "beta/0/gpu");
    source.end();
}

/** @brief Tests that begin() starts the delivery and end() stops it and drops what was never collected. */
TEST_F(Small_DeviceSource, EndStopsTheDeliveryAndDropsWhatWasNeverCollected)
{
    EXPECT_EQ(attachment_.attaches, 1);
    push(observation_of({device_record()}, {row_of(0, "conv")}));
    ASSERT_EQ(queue_.size(), 1u);

    source_.end();

    EXPECT_EQ(attachment_.detaches, 1);
    EXPECT_EQ(queue_.size(), 0u);
}

/** @brief Tests that a source whose delivery cannot start reports why and does not begin. */
TEST(Small_DeviceSourceStart, BeginFailsWhenTheAttachmentRefuses)
{
    SushiProfiler::NameTable names;
    std::vector<std::string> tracks;
    SushiProfiler::SourceContext context(names, tracks, K_ORIGIN);
    RunQueue queue(8);
    FakeAttachment attachment;
    attachment.refusal = "a runtime already has an observer";
    DeviceSource source(queue, {RuntimeTracks{"node", {"gpu"}}}, &attachment);

    const SushiProfiler::SourceStatus status = source.begin(context);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error, "a runtime already has an observer");
}

/** @brief Tests the name the registry knows the source by. */
TEST(Small_DeviceSourceStart, TheSourceIsNamedSushiruntimeDevice)
{
    RunQueue queue(1);
    const DeviceSource source(queue, {});

    EXPECT_EQ(source.name(), "sushiruntime.device");
}
