/**************************************************************************/
/* test_layer_equivalence.cpp                                             */
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
 * @file test_layer_equivalence.cpp
 * @brief Tests that four graph shapes give byte-identical output on every path that runs them.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    /** @brief Names the element type of every buffer here; unsigned, so overflow wraps. */
    using Word = std::uint32_t;

    /** @brief Names one output buffer copied to the host. */
    using Image = std::vector<Word>;

    constexpr std::size_t TILE_N = 512;
    constexpr std::size_t HOST_N = 256;
    constexpr std::size_t STEPS  = 5;
    constexpr std::size_t RUNS   = 3;

    constexpr std::size_t GRID_ROWS = 12;
    constexpr std::size_t GRID_COLS = 16;
    constexpr std::size_t GRID_N    = GRID_ROWS * GRID_COLS;
    constexpr std::size_t BOX_ROWS  = 6;
    constexpr std::size_t BOX_COLS  = 8;
    constexpr std::size_t BOX_N     = BOX_ROWS * BOX_COLS;
    constexpr std::size_t BOX_ROW0  = 3;
    constexpr std::size_t BOX_COL0  = 5;
    constexpr std::size_t TILE_ROWS = 3;
    constexpr std::size_t TILE_COLS = 4;

    constexpr API::DynamicGraph::RegionKey KEY_SCALE = 10;
    constexpr API::DynamicGraph::RegionKey KEY_MIX   = 20;
    constexpr API::DynamicGraph::RegionKey KEY_FOLD  = 30;
    constexpr API::DynamicGraph::RegionKey KEY_FIELD = 40;

    /** @brief Holds every output of one workload run, copied out, in a fixed order. */
    struct Snapshot
    {
        std::vector<const char*> names; /**< One label per image, for failure messages. */
        std::vector<Image> images;      /**< The output buffers, in the order they were taken. */
        /** @brief Layer the last blocking run reported. */
        Core::ExecutionLayer layer = Core::ExecutionLayer::Scheduler;
    };

    /** @brief Appends one named output to a snapshot. */
    void take(Snapshot& shot, const char* name, Image image)
    {
        shot.names.push_back(name);
        shot.images.push_back(std::move(image));
    }

    /** @brief Returns the index of the first element whose bytes differ, or the common size. */
    std::size_t first_difference(const Image& a, const Image& b)
    {
        const std::size_t n = (a.size() < b.size()) ? a.size() : b.size();
        for (std::size_t i = 0; i < n; ++i)
            if (std::memcmp(&a[i], &b[i], sizeof(Word)) != 0)
                return i;
        return n;
    }

    /**
     * @brief Asserts two snapshots hold the same bytes, image by image.
     * @param pairing Names the two runs compared, for the failure message.
     */
    void expect_identical(const Snapshot& expected, const Snapshot& actual, const char* pairing)
    {
        ASSERT_EQ(expected.images.size(), actual.images.size()) << pairing;
        for (std::size_t k = 0; k < expected.images.size(); ++k)
        {
            const Image& a = expected.images[k];
            const Image& b = actual.images[k];
            ASSERT_EQ(a.size(), b.size()) << pairing << ": " << expected.names[k];
            EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size() * sizeof(Word)), 0)
                << pairing << ": " << expected.names[k] << " differs first at element "
                << first_difference(a, b);
        }
    }

    /** @brief Returns a configuration that keeps every plan on the scheduler layer. */
    API::RuntimeConfig scheduler_only()
    {
        API::RuntimeConfig config;
        config.force_scheduler = true;
        return config;
    }

    /** @brief Says how a graph is stepped. */
    enum class Drive : std::uint8_t
    {
        Blocking,     /**< One run(steps) call. */
        Asynchronous, /**< One run_async() and wait() per step. */
    };

    /**
     * @brief Steps a graph the given way and records the layer a blocking run reports.
     * @tparam Runnable API::Graph or API::DynamicGraph.
     */
    template <class Runnable>
    void drive(Runnable& graph, Drive how, std::size_t steps, Snapshot& shot)
    {
        if (how == Drive::Blocking)
        {
            const Core::RunReport report = graph.run(steps);
            EXPECT_TRUE(report.is_successful);
            shot.layer = report.layer;
            return;
        }
        for (std::size_t s = 0; s < steps; ++s)
            graph.run_async().wait();
    }

    /** @brief Owns the data of the tile workload: three chained buffers and one State. */
    struct Tiles
    {
        /** @brief Allocates the data on @p rt and writes the starting values. */
        explicit Tiles(Runtime& rt)
            : scale(rt.buffer<Word>(TILE_N)),
              mix(rt.buffer<Word>(TILE_N)),
              fold(rt.buffer<Word>(TILE_N)),
              field(rt.state<Word>(TILE_N))
        {
            for (std::size_t i = 0; i < TILE_N; ++i)
            {
                scale[i] = static_cast<Word>(i) * 3u + 1u;
                mix[i]   = static_cast<Word>(i) ^ 0x9E3779B9u;
                fold[i]  = 0u;
                field[i] = static_cast<Word>(i) + 11u;
            }
        }

        API::Buffer<Word> scale; /**< Rewritten from itself every step. */
        API::Buffer<Word> mix;   /**< Reads scale, so it orders after it. */
        API::Buffer<Word> fold;  /**< Reads scale and mix, so it orders last. */
        API::State<Word> field;  /**< Evolves on its own, independent of the buffers. */
    };

    /** @brief Copies every tile output into a snapshot. */
    void take_tiles(Snapshot& shot, const Tiles& tiles)
    {
        take(shot, "scale", Image(tiles.scale.data(), tiles.scale.data() + TILE_N));
        take(shot, "mix", Image(tiles.mix.data(), tiles.mix.data() + TILE_N));
        take(shot, "fold", Image(tiles.fold.data(), tiles.fold.data() + TILE_N));
        take(shot, "field", Image(tiles.field.current(), tiles.field.current() + TILE_N));
    }

    /**
     * @brief Records the kernel that rewrites the scale buffer.
     * @tparam Builder API::Graph or API::Region.
     */
    template <class Builder>
    void record_scale(Builder& builder, Tiles& tiles)
    {
        Word* scale = tiles.scale.data();
        builder.add(API::Reads(tiles.scale), API::Writes(tiles.scale), TILE_N,
                    [scale](std::size_t i)
                    {
                        scale[i] = scale[i] * 31u + static_cast<Word>(i) + 1u;
                    });
    }

    /**
     * @brief Records the kernel that mixes scale into the mix buffer.
     * @tparam Builder API::Graph or API::Region.
     */
    template <class Builder>
    void record_mix(Builder& builder, Tiles& tiles)
    {
        const Word* scale = tiles.scale.data();
        Word* mix = tiles.mix.data();
        builder.add(API::Reads(tiles.scale), API::Writes(tiles.mix), TILE_N,
                    [scale, mix](std::size_t i)
                    {
                        mix[i] = (mix[i] ^ (scale[i] * 2654435761u)) + 7u;
                    });
    }

    /**
     * @brief Records the kernel that folds scale and mix into the fold buffer.
     * @tparam Builder API::Graph or API::Region.
     */
    template <class Builder>
    void record_fold(Builder& builder, Tiles& tiles)
    {
        const Word* scale = tiles.scale.data();
        const Word* mix = tiles.mix.data();
        Word* fold = tiles.fold.data();
        builder.add(API::Reads(tiles.scale, tiles.mix), API::Writes(tiles.fold), TILE_N,
                    [scale, mix, fold](std::size_t i)
                    {
                        fold[i] = fold[i] * 17u + scale[i] + mix[i];
                    });
    }

    /**
     * @brief Records the per-step update rule of the State.
     * @tparam Builder API::Graph or API::Region.
     */
    template <class Builder>
    void record_field(Builder& builder, Tiles& tiles)
    {
        builder.add(tiles.field, [](std::size_t i, const Word* cur, Word* next)
        {
            next[i] = cur[i] * 5u + static_cast<Word>(i % 7);
        });
    }

    /** @brief Describes one stretch of a tile run with unchanged recorded work. */
    struct Segment
    {
        bool mix_live;    /**< Whether the mix kernel is part of the work. */
        std::size_t runs; /**< Number of drive() calls, each of STEPS steps. */
    };

    /** @brief Describes a whole tile run. */
    struct Script
    {
        std::vector<Segment> segments; /**< The stretches, in order, on the same data. */
        bool evolve_field;             /**< Whether the State rule is recorded. */
    };

    /** @brief Runs one segment as a static graph built for exactly that segment's work. */
    void run_static_segment(Runtime& rt, Tiles& tiles, const Segment& segment,
                            bool evolve_field, Drive how, Snapshot& shot)
    {
        auto g = rt.graph();
        record_scale(g, tiles);
        if (segment.mix_live)
            record_mix(g, tiles);
        record_fold(g, tiles);
        if (evolve_field)
            record_field(g, tiles);

        for (std::size_t r = 0; r < segment.runs; ++r)
            drive(g, how, STEPS, shot);
    }

    /** @brief Runs a script with one static graph per segment and copies the results out. */
    Snapshot run_static_tiles(Runtime& rt, const Script& script, Drive how)
    {
        Tiles tiles(rt);
        Snapshot shot;
        for (const Segment& segment : script.segments)
            run_static_segment(rt, tiles, segment, script.evolve_field, how, shot);
        take_tiles(shot, tiles);
        return shot;
    }

    /**
     * @brief Runs a script on one DynamicGraph, one region per kernel, and copies the results out.
     *
     * The mix region is dropped or recorded again where a segment changes it.
     */
    Snapshot run_dynamic_tiles(Runtime& rt, const Script& script, Drive how)
    {
        Tiles tiles(rt);
        Snapshot shot;

        // Region keys ascend in the order the static graph adds the same kernels.
        auto world = rt.dynamic_graph();
        record_scale(world.region(KEY_SCALE), tiles);
        record_fold(world.region(KEY_FOLD), tiles);
        if (script.evolve_field)
            record_field(world.region(KEY_FIELD), tiles);

        for (const Segment& segment : script.segments)
        {
            if (segment.mix_live && !world.has_region(KEY_MIX))
                record_mix(world.region(KEY_MIX), tiles);
            if (!segment.mix_live)
                world.drop(KEY_MIX);

            for (std::size_t r = 0; r < segment.runs; ++r)
                drive(world, how, STEPS, shot);
        }

        take_tiles(shot, tiles);
        return shot;
    }

    /** @brief Folds one buffer into another, element by element. */
    struct MixPass
    {
        const Word* from; /**< HOST_N elements read. */
        Word* into;       /**< HOST_N elements rewritten. */

        /** @brief Runs the whole pass on the calling thread or work-item. */
        void apply() const noexcept
        {
            for (std::size_t i = 0; i < HOST_N; ++i)
                into[i] = (into[i] ^ (from[i] * 2654435761u)) + 7u;
        }
    };

    /** @brief Folds a running value over one buffer into another, in index order. */
    struct ScanPass
    {
        const Word* from; /**< HOST_N elements read in index order. */
        Word* into;       /**< HOST_N elements rewritten. */

        /** @brief Runs the whole pass on the calling thread or work-item. */
        void apply() const noexcept
        {
            Word running = 0u;
            for (std::size_t i = 0; i < HOST_N; ++i)
            {
                running = running * 33u + from[i];
                into[i] ^= running;
            }
        }
    };

    /** @brief Says where a whole-buffer pass runs. */
    enum class PassSite : std::uint8_t
    {
        Host,   /**< As a host task, through add_host(). */
        Device, /**< As a one-item device kernel, through add(). */
    };

    /**
     * @brief Records one whole-buffer pass as a host task or as a one-item kernel.
     * @tparam Pass A trivially copyable type with a const apply().
     */
    template <class Pass>
    void record_pass(API::Graph& g, const API::Reads& reads, const API::Writes& writes,
                     Pass pass, PassSite site)
    {
        if (site == PassSite::Host)
        {
            g.add_host(reads, writes, [pass] { pass.apply(); });
            return;
        }
        g.add(reads, writes, std::size_t{1}, [pass](std::size_t) { pass.apply(); });
    }

    /**
     * @brief Runs a five-task dependency chain in which two passes sit between device kernels.
     * @param site Where the two passes run; the three kernels always run on the device.
     */
    Snapshot run_pass_chain(Runtime& rt, PassSite site)
    {
        auto a = rt.buffer<Word>(HOST_N);
        auto b = rt.buffer<Word>(HOST_N);
        auto c = rt.buffer<Word>(HOST_N);
        auto d = rt.buffer<Word>(HOST_N);
        for (std::size_t i = 0; i < HOST_N; ++i)
        {
            a[i] = static_cast<Word>(i) * 7u + 3u;
            b[i] = static_cast<Word>(i) ^ 0x85EBCA6Bu;
            c[i] = 1u;
            d[i] = 0u;
        }

        Word* pa = a.data();
        Word* pb = b.data();
        Word* pc = c.data();
        Word* pd = d.data();

        auto g = rt.graph();
        g.add(API::Reads(a), API::Writes(a), HOST_N,
              [pa](std::size_t i) { pa[i] = pa[i] * 31u + static_cast<Word>(i) + 1u; });
        record_pass(g, API::Reads(a), API::Writes(b), MixPass{pa, pb}, site);
        g.add(API::Reads(b), API::Writes(c), HOST_N,
              [pb, pc](std::size_t i) { pc[i] = pc[i] * 17u + pb[i]; });
        record_pass(g, API::Reads(c), API::Writes(d), ScanPass{pc, pd}, site);
        g.add(API::Reads(a, d), API::Writes(a), HOST_N,
              [pa, pd](std::size_t i) { pa[i] += pd[i]; });

        Snapshot shot;
        for (std::size_t r = 0; r < RUNS; ++r)
            drive(g, Drive::Blocking, STEPS, shot);

        take(shot, "a", Image(pa, pa + HOST_N));
        take(shot, "b", Image(pb, pb + HOST_N));
        take(shot, "c", Image(pc, pc + HOST_N));
        take(shot, "d", Image(pd, pd + HOST_N));
        return shot;
    }

    /** @brief Returns the read-only input word of a grid cell. */
    constexpr Word source_word(std::size_t cell) noexcept
    {
        return static_cast<Word>(cell) * 2246822519u + 5u;
    }

    /** @brief Returns the starting word of a grid cell in every output. */
    constexpr Word seed_word(std::size_t cell) noexcept
    {
        return static_cast<Word>(cell) ^ 0xC2B2AE35u;
    }

    /** @brief Returns the next value of a cell from its previous value, its input and its index. */
    constexpr Word stamp(Word previous, Word input, std::size_t cell) noexcept
    {
        return previous * 1664525u + (input ^ static_cast<Word>(cell)) + 1013904223u;
    }

    /** @brief Advances one grid cell in place, addressed by absolute row and column. */
    inline void stamp_cell(const Word* in, Word* out, std::size_t row, std::size_t col) noexcept
    {
        const std::size_t cell = row * GRID_COLS + col;
        out[cell] = stamp(out[cell], in[cell], cell);
    }

    /** @brief Returns the launch box given by a range and an origin. */
    Extent<2> box_by_origin()
    {
        return Extent<2>(sycl::range<2>(BOX_ROWS, BOX_COLS), sycl::id<2>(BOX_ROW0, BOX_COL0));
    }

    /** @brief Returns the same launch box given by sizes and rebased with at(). */
    Extent<2> box_by_rebase()
    {
        return Extent{BOX_ROWS, BOX_COLS}.at(sycl::id<2>(BOX_ROW0, BOX_COL0));
    }

    /** @brief Returns the box-sized tiled launch; NdRange carries no origin. */
    NdRange<2> box_tiles()
    {
        return NdRange{sycl::range<2>(BOX_ROWS, BOX_COLS), sycl::range<2>(TILE_ROWS, TILE_COLS)};
    }

    /** @brief Returns the grid a host loop reaches after @p steps steps over the box. */
    Image reference_grid(std::size_t steps)
    {
        Image source(GRID_N);
        Image grid(GRID_N);
        for (std::size_t k = 0; k < GRID_N; ++k)
        {
            source[k] = source_word(k);
            grid[k]   = seed_word(k);
        }
        for (std::size_t s = 0; s < steps; ++s)
            for (std::size_t row = BOX_ROW0; row < BOX_ROW0 + BOX_ROWS; ++row)
                for (std::size_t col = BOX_COL0; col < BOX_COL0 + BOX_COLS; ++col)
                    stamp_cell(source.data(), grid.data(), row, col);
        return grid;
    }

    /** @brief Returns the box cells of a grid, packed row by row. */
    Image box_of(const Image& grid)
    {
        Image box(BOX_N);
        for (std::size_t row = 0; row < BOX_ROWS; ++row)
            for (std::size_t col = 0; col < BOX_COLS; ++col)
                box[row * BOX_COLS + col] = grid[(row + BOX_ROW0) * GRID_COLS + col + BOX_COL0];
        return box;
    }

    /** @brief Allocates a grid-sized buffer on @p rt holding the starting words. */
    API::Buffer<Word> seeded_grid(Runtime& rt)
    {
        auto grid = rt.buffer<Word>(GRID_N);
        for (std::size_t k = 0; k < GRID_N; ++k)
            grid[k] = seed_word(k);
        return grid;
    }

    /** @brief Allocates the read-only input grid on @p rt. */
    API::Buffer<Word> source_grid(Runtime& rt)
    {
        auto grid = rt.buffer<Word>(GRID_N);
        for (std::size_t k = 0; k < GRID_N; ++k)
            grid[k] = source_word(k);
        return grid;
    }

    /**
     * @brief Runs the offset box through every tracked launch form, each writing its own output.
     *
     * The State is box-sized: its slot is the cell's position inside the box.
     */
    Snapshot run_tracked_launches(Runtime& rt)
    {
        auto source     = source_grid(rt);
        auto by_origin  = seeded_grid(rt);
        auto by_rebase  = seeded_grid(rt);
        auto by_capture = seeded_grid(rt);
        auto by_tiles   = seeded_grid(rt);
        auto field      = rt.state<Word>(BOX_N);

        const Image seeded_box = box_of(reference_grid(0));
        for (std::size_t slot = 0; slot < BOX_N; ++slot)
            field[slot] = seeded_box[slot];

        const Word* in = source.data();
        Word* origin_out = by_origin.data();
        Word* rebase_out = by_rebase.data();
        Word* tiles_out  = by_tiles.data();

        auto g = rt.graph();
        g.add(API::Reads(source, by_origin), API::Writes(by_origin), box_by_origin(),
              [in, origin_out](sycl::id<2> idx) { stamp_cell(in, origin_out, idx[0], idx[1]); });
        g.add(API::Reads(source, by_rebase), API::Writes(by_rebase), box_by_rebase(),
              [in, rebase_out](sycl::id<2> idx) { stamp_cell(in, rebase_out, idx[0], idx[1]); });
        g.add(box_by_origin(), In(source), InOut(by_capture),
              [](sycl::id<2> idx, const Word* captured_in, Word* captured_out)
              {
                  stamp_cell(captured_in, captured_out, idx[0], idx[1]);
              });
        g.add(field, box_by_origin(), [in](sycl::id<2> idx, const Word* cur, Word* next)
        {
            const std::size_t cell = idx[0] * GRID_COLS + idx[1];
            const std::size_t slot = (idx[0] - BOX_ROW0) * BOX_COLS + (idx[1] - BOX_COL0);
            next[slot] = stamp(cur[slot], in[cell], cell);
        });
        g.add(API::Reads(source, by_tiles), API::Writes(by_tiles), box_tiles(),
              [in, tiles_out](sycl::nd_item<2> item)
              {
                  stamp_cell(in, tiles_out, item.get_global_id(0) + BOX_ROW0,
                             item.get_global_id(1) + BOX_COL0);
              });

        Snapshot shot;
        drive(g, Drive::Blocking, STEPS, shot);

        take(shot, "Extent(range, origin)", Image(origin_out, origin_out + GRID_N));
        take(shot, "Extent{sizes}.at(origin)", Image(rebase_out, rebase_out + GRID_N));
        take(shot, "Extent with In/InOut", Image(by_capture.data(), by_capture.data() + GRID_N));
        take(shot, "NdRange", Image(tiles_out, tiles_out + GRID_N));
        take(shot, "State over Extent", Image(field.current(), field.current() + BOX_N));
        return shot;
    }

    /** @brief Returns what run_tracked_launches() must produce, computed on the host. */
    Snapshot tracked_launch_reference()
    {
        const Image grid = reference_grid(STEPS);
        Snapshot shot;
        take(shot, "Extent(range, origin)", grid);
        take(shot, "Extent{sizes}.at(origin)", grid);
        take(shot, "Extent with In/InOut", grid);
        take(shot, "NdRange", grid);
        take(shot, "State over Extent", box_of(grid));
        return shot;
    }

    /** @brief Runs the offset box through both untracked launch forms, one output each. */
    Snapshot run_untracked_launches(Runtime& rt)
    {
        auto source    = source_grid(rt);
        auto by_extent = seeded_grid(rt);
        auto by_tiles  = seeded_grid(rt);

        const Word* in = source.data();
        Word* extent_out = by_extent.data();
        Word* tiles_out  = by_tiles.data();

        // The two kernels are unordered; each is the only writer of its own buffer.
        auto g = rt.graph();
        g.add_untracked(box_by_origin(), [in, extent_out](sycl::id<2> idx)
        {
            stamp_cell(in, extent_out, idx[0], idx[1]);
        });
        g.add_untracked(box_tiles(), [in, tiles_out](sycl::nd_item<2> item)
        {
            stamp_cell(in, tiles_out, item.get_global_id(0) + BOX_ROW0,
                       item.get_global_id(1) + BOX_COL0);
        });

        Snapshot shot;
        drive(g, Drive::Blocking, STEPS, shot);

        take(shot, "untracked Extent", Image(extent_out, extent_out + GRID_N));
        take(shot, "untracked NdRange", Image(tiles_out, tiles_out + GRID_N));
        return shot;
    }

    /** @brief Returns what run_untracked_launches() must produce, computed on the host. */
    Snapshot untracked_launch_reference()
    {
        const Image grid = reference_grid(STEPS);
        Snapshot shot;
        take(shot, "untracked Extent", grid);
        take(shot, "untracked NdRange", grid);
        return shot;
    }
} // namespace

/** @brief Tests that four regions of a DynamicGraph leave the bytes one static Graph leaves. */
TEST(Integration_LayerEquivalence, DynamicRegionsMatchOneStaticGraph)
{
    const Script script{{{true, RUNS}}, true};

    auto chained_rt = Runtime::create();
    auto scheduled_rt = Runtime::create({}, scheduler_only());

    const Snapshot static_chained    = run_static_tiles(chained_rt, script, Drive::Blocking);
    const Snapshot static_scheduled  = run_static_tiles(scheduled_rt, script, Drive::Blocking);
    const Snapshot dynamic_default   = run_dynamic_tiles(chained_rt, script, Drive::Blocking);
    const Snapshot dynamic_scheduled = run_dynamic_tiles(scheduled_rt, script, Drive::Blocking);

    EXPECT_EQ(static_chained.layer, Core::ExecutionLayer::Chain)
        << "the static graph of tracked kernels must take the chain";
    EXPECT_EQ(static_scheduled.layer, Core::ExecutionLayer::Scheduler);

    expect_identical(static_chained, static_scheduled, "static chain vs static scheduler");
    expect_identical(static_chained, dynamic_default, "static chain vs dynamic");
    expect_identical(static_chained, dynamic_scheduled,
                     "static chain vs dynamic, forced scheduler");
}

/** @brief Tests that a region dropped and recorded again matches a static graph per stretch. */
TEST(Integration_LayerEquivalence, DroppedAndReaddedRegionMatchesStaticGraphs)
{
    const Script script{{{true, 1}, {false, 1}, {true, 1}}, false};

    auto chained_rt = Runtime::create();
    auto scheduled_rt = Runtime::create({}, scheduler_only());

    const Snapshot static_chained    = run_static_tiles(chained_rt, script, Drive::Blocking);
    const Snapshot static_scheduled  = run_static_tiles(scheduled_rt, script, Drive::Blocking);
    const Snapshot dynamic_default   = run_dynamic_tiles(chained_rt, script, Drive::Blocking);
    const Snapshot dynamic_scheduled = run_dynamic_tiles(scheduled_rt, script, Drive::Blocking);

    EXPECT_EQ(static_chained.layer, Core::ExecutionLayer::Chain);
    EXPECT_EQ(static_scheduled.layer, Core::ExecutionLayer::Scheduler);

    expect_identical(static_chained, static_scheduled, "static chain vs static scheduler");
    expect_identical(static_chained, dynamic_default, "static chain vs dynamic");
    expect_identical(static_chained, dynamic_scheduled,
                     "static chain vs dynamic, forced scheduler");
}

/** @brief Tests that host tasks between kernels give the same bytes on both layers. */
TEST(Integration_LayerEquivalence, HostTasksAmongKernelsMatchAcrossLayers)
{
    auto default_rt = Runtime::create();
    auto scheduled_rt = Runtime::create({}, scheduler_only());

    const Snapshot host_default   = run_pass_chain(default_rt, PassSite::Host);
    const Snapshot host_scheduled = run_pass_chain(scheduled_rt, PassSite::Host);
    const Snapshot device_chained = run_pass_chain(default_rt, PassSite::Device);

    EXPECT_EQ(host_scheduled.layer, Core::ExecutionLayer::Scheduler);
    EXPECT_EQ(device_chained.layer, Core::ExecutionLayer::Chain)
        << "the same chain written as kernels only must take the chain";

    expect_identical(host_default, host_scheduled, "host tasks: default vs forced scheduler");
    expect_identical(device_chained, host_default, "kernels on the chain vs host tasks");
}

/** @brief Tests that N run_async() steps leave the bytes run(N) leaves, on both layers. */
TEST(Integration_LayerEquivalence, AsyncStepsMatchBlockingRun)
{
    const Script script{{{true, RUNS}}, true};

    auto chained_rt = Runtime::create();
    auto scheduled_rt = Runtime::create({}, scheduler_only());

    const Snapshot blocking_chained   = run_static_tiles(chained_rt, script, Drive::Blocking);
    const Snapshot blocking_scheduled = run_static_tiles(scheduled_rt, script, Drive::Blocking);
    const Snapshot async_default      = run_static_tiles(chained_rt, script, Drive::Asynchronous);
    const Snapshot async_scheduled    = run_static_tiles(scheduled_rt, script, Drive::Asynchronous);

    EXPECT_EQ(blocking_chained.layer, Core::ExecutionLayer::Chain);
    EXPECT_EQ(blocking_scheduled.layer, Core::ExecutionLayer::Scheduler);

    expect_identical(blocking_chained, async_default, "run(N) on the chain vs run_async");
    expect_identical(blocking_scheduled, async_scheduled, "run(N) vs run_async, forced scheduler");
    expect_identical(blocking_chained, blocking_scheduled, "run(N): chain vs forced scheduler");
}

/** @brief Tests that a DynamicGraph stepped through run_async() matches its blocking run. */
TEST(Integration_LayerEquivalence, AsyncStepsOfADynamicGraphMatchBlockingRun)
{
    const Script script{{{true, 1}, {false, 1}, {true, 1}}, true};

    auto rt = Runtime::create();

    const Snapshot blocking = run_dynamic_tiles(rt, script, Drive::Blocking);
    const Snapshot stepped  = run_dynamic_tiles(rt, script, Drive::Asynchronous);

    expect_identical(blocking, stepped, "dynamic run(N) vs dynamic run_async");
}

/** @brief Tests that every tracked form of an offset 2-D launch matches the host reference. */
TEST(Integration_LayerEquivalence, OffsetLaunchFormsMatchAcrossLayers)
{
    auto chained_rt = Runtime::create();
    auto scheduled_rt = Runtime::create({}, scheduler_only());

    const Snapshot reference = tracked_launch_reference();
    const Snapshot chained   = run_tracked_launches(chained_rt);
    const Snapshot scheduled = run_tracked_launches(scheduled_rt);

    EXPECT_EQ(chained.layer, Core::ExecutionLayer::Chain)
        << "tracked Extent and NdRange kernels on one device must take the chain";
    EXPECT_EQ(scheduled.layer, Core::ExecutionLayer::Scheduler);

    expect_identical(reference, chained, "host reference vs chain");
    expect_identical(reference, scheduled, "host reference vs forced scheduler");
    expect_identical(chained, scheduled, "chain vs forced scheduler");
}

/** @brief Tests that the untracked Extent and NdRange launches match the host reference. */
TEST(Integration_LayerEquivalence, UntrackedOffsetLaunchesMatchAcrossLayers)
{
    auto default_rt = Runtime::create();

    // An untracked kernel is routed to any device (docs/reference/KNOWN_ISSUES.md).
    if (default_rt.advanced().device_count() > 1)
        GTEST_SKIP() << "An untracked kernel may run on a device that does not own its buffers.";
    auto scheduled_rt = Runtime::create({}, scheduler_only());

    const Snapshot reference       = untracked_launch_reference();
    const Snapshot default_layer   = run_untracked_launches(default_rt);
    const Snapshot scheduler_layer = run_untracked_launches(scheduled_rt);

    EXPECT_EQ(scheduler_layer.layer, Core::ExecutionLayer::Scheduler);

    expect_identical(reference, default_layer, "host reference vs default layer");
    expect_identical(reference, scheduler_layer, "host reference vs forced scheduler");
    expect_identical(default_layer, scheduler_layer, "default layer vs forced scheduler");
}
