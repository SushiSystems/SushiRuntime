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
 * @file test_layer_equivalence.cpp
 * @brief Tests that graph shapes leave the bytes a host model of their work leaves, on the chain and on the scheduler.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "run_recorder.hpp"
#include "support/host_runtime.hpp"
#include "support/runtime_environment.hpp"
#include "support/submitting_threads.hpp"

using namespace SushiRuntime;

/** @brief Tests that graph shapes leave the bytes a host model of their work leaves, on the chain and on the scheduler. */
namespace LayerEquivalenceTests
{
    namespace
    {
        /** @brief Names the element type of every buffer here; unsigned, so overflow wraps. */
        using Word = std::uint32_t;

        /** @brief Names one output buffer copied to the host. */
        using Image = std::vector<Word>;

        constexpr std::size_t K_TILE_N = 512;
        constexpr std::size_t K_HOST_N = 256;
        constexpr std::size_t K_STEPS  = 5;
        constexpr std::size_t K_RUNS   = 3;

        constexpr std::size_t K_GRID_ROWS = 12;
        constexpr std::size_t K_GRID_COLS = 16;
        constexpr std::size_t K_GRID_N    = K_GRID_ROWS * K_GRID_COLS;
        constexpr std::size_t K_BOX_ROWS  = 6;
        constexpr std::size_t K_BOX_COLS  = 8;
        constexpr std::size_t K_BOX_N     = K_BOX_ROWS * K_BOX_COLS;
        constexpr std::size_t K_BOX_ROW0  = 3;
        constexpr std::size_t K_BOX_COL0  = 5;
        constexpr std::size_t K_TILE_ROWS = 3;
        constexpr std::size_t K_TILE_COLS = 4;

        constexpr DynamicGraph::RegionKey K_KEY_SCALE = 10;
        constexpr DynamicGraph::RegionKey K_KEY_MIX   = 20;
        constexpr DynamicGraph::RegionKey K_KEY_FOLD  = 30;
        constexpr DynamicGraph::RegionKey K_KEY_FIELD = 40;

        /** @brief Holds every output of one workload run, copied out, in a fixed order. */
        struct Snapshot
        {
            std::vector<const char*> names; /**< One label per image, for failure messages. */
            std::vector<Image> images;      /**< The output buffers, in the order they were taken. */
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

        /** @brief Says how a graph is stepped. */
        enum class Drive : std::uint8_t
        {
            Blocking,     /**< One run(steps) call. */
            Asynchronous, /**< One run_async() and wait() per step. */
        };

        /**
         * @brief Steps a graph the given way.
         * @tparam Runnable Graph or DynamicGraph.
         */
        template <class Runnable>
        void drive(Runnable& graph, Drive how, std::size_t steps)
        {
            if (how == Drive::Blocking)
            {
                const RunReport report = graph.run(steps).value();
                EXPECT_TRUE(report.is_successful);
                return;
            }
            for (std::size_t s = 0; s < steps; ++s)
                graph.run_async().value().wait().value();
        }

        /** @brief Owns the data of the tile workload: three chained buffers and one State. */
        struct Tiles
        {
            /** @brief Allocates the data on @p rt and writes the starting values. */
            explicit Tiles(const Runtime& rt)
                : scale(rt.buffer<Word>(K_TILE_N).value()),
                  mix(rt.buffer<Word>(K_TILE_N).value()),
                  fold(rt.buffer<Word>(K_TILE_N).value()),
                  field(rt.state<Word>(K_TILE_N).value())
            {
                for (std::size_t i = 0; i < K_TILE_N; ++i)
                {
                    scale[i] = static_cast<Word>(i) * 3u + 1u;
                    mix[i]   = static_cast<Word>(i) ^ 0x9E3779B9u;
                    fold[i]  = 0u;
                    field[i] = static_cast<Word>(i) + 11u;
                }
            }

            Buffer<Word> scale; /**< Rewritten from itself every step. */
            Buffer<Word> mix;   /**< Reads scale, so it orders after it. */
            Buffer<Word> fold;  /**< Reads scale and mix, so it orders last. */
            State<Word> field;  /**< Evolves on its own, independent of the buffers. */
        };

        /** @brief Copies every tile output into a snapshot. */
        void take_tiles(Snapshot& shot, const Tiles& tiles)
        {
            take(shot, "scale", Image(tiles.scale.data(), tiles.scale.data() + K_TILE_N));
            take(shot, "mix", Image(tiles.mix.data(), tiles.mix.data() + K_TILE_N));
            take(shot, "fold", Image(tiles.fold.data(), tiles.fold.data() + K_TILE_N));
            take(shot, "field", Image(tiles.field.current(), tiles.field.current() + K_TILE_N));
        }

        /**
         * @brief Records the kernel that rewrites the scale buffer.
         * @tparam Builder Graph or Region.
         */
        template <class Builder>
        void record_scale(Builder& builder, Tiles& tiles)
        {
            Word* scale = tiles.scale.data();
            builder.add(K_TILE_N, Reads(tiles.scale), Writes(tiles.scale),
                        [scale](std::size_t i)
                        {
                            scale[i] = scale[i] * 31u + static_cast<Word>(i) + 1u;
                        });
        }

        /**
         * @brief Records the kernel that mixes scale into the mix buffer.
         * @tparam Builder Graph or Region.
         */
        template <class Builder>
        void record_mix(Builder& builder, Tiles& tiles)
        {
            const Word* scale = tiles.scale.data();
            Word* mix = tiles.mix.data();
            builder.add(K_TILE_N, Reads(tiles.scale), Writes(tiles.mix),
                        [scale, mix](std::size_t i)
                        {
                            mix[i] = (mix[i] ^ (scale[i] * 2654435761u)) + 7u;
                        });
        }

        /**
         * @brief Records the kernel that folds scale and mix into the fold buffer.
         * @tparam Builder Graph or Region.
         */
        template <class Builder>
        void record_fold(Builder& builder, Tiles& tiles)
        {
            const Word* scale = tiles.scale.data();
            const Word* mix = tiles.mix.data();
            Word* fold = tiles.fold.data();
            builder.add(K_TILE_N, Reads(tiles.scale, tiles.mix), Writes(tiles.fold),
                        [scale, mix, fold](std::size_t i)
                        {
                            fold[i] = fold[i] * 17u + scale[i] + mix[i];
                        });
        }

        /**
         * @brief Records the per-step update rule of the State.
         * @tparam Builder Graph or Region.
         */
        template <class Builder>
        void record_field(Builder& builder, Tiles& tiles)
        {
            builder.add_rule(tiles.field, [](std::size_t i, const Word* cur, Word* next)
            {
                next[i] = cur[i] * 5u + static_cast<Word>(i % 7);
            });
        }

        /** @brief Describes one stretch of a tile run with unchanged recorded work. */
        struct Segment
        {
            bool mix_live;    /**< Whether the mix kernel is part of the work. */
            std::size_t runs; /**< Number of drive() calls, each of K_STEPS steps. */
        };

        /** @brief Describes a whole tile run. */
        struct Script
        {
            std::vector<Segment> segments; /**< The stretches, in order, on the same data. */
            bool evolve_field;             /**< Whether the State rule is recorded. */
        };

        /** @brief Runs one segment as a static graph built for exactly that segment's work. */
        void run_static_segment(const Runtime& rt, Tiles& tiles, const Segment& segment,
                                bool evolve_field, Drive how)
        {
            auto g = rt.graph();
            record_scale(g, tiles);
            if (segment.mix_live)
                record_mix(g, tiles);
            record_fold(g, tiles);
            if (evolve_field)
                record_field(g, tiles);

            for (std::size_t r = 0; r < segment.runs; ++r)
                drive(g, how, K_STEPS);
        }

        /** @brief Runs a script with one static graph per segment and copies the results out. */
        Snapshot run_static_tiles(const Runtime& rt, const Script& script, Drive how)
        {
            Tiles tiles(rt);
            Snapshot shot;
            for (const Segment& segment : script.segments)
                run_static_segment(rt, tiles, segment, script.evolve_field, how);
            take_tiles(shot, tiles);
            return shot;
        }

        /** @brief Records the regions of a script that every segment keeps, one region per kernel. */
        void record_kept_regions(DynamicGraph& world, Tiles& tiles, const Script& script)
        {
            // Region keys ascend in the order the static graph adds the same kernels.
            record_scale(world.region(K_KEY_SCALE), tiles);
            record_fold(world.region(K_KEY_FOLD), tiles);
            if (script.evolve_field)
                record_field(world.region(K_KEY_FIELD), tiles);
        }

        /** @brief Records the mix region again or drops it, as a segment says. */
        void shape_mix_region(DynamicGraph& world, Tiles& tiles, const Segment& segment)
        {
            if (segment.mix_live && !world.has_region(K_KEY_MIX))
                record_mix(world.region(K_KEY_MIX), tiles);
            if (!segment.mix_live)
                world.drop(K_KEY_MIX);
        }

        /**
         * @brief Runs a script on one DynamicGraph, one region per kernel, and copies the results out.
         *
         * The mix region is dropped or recorded again where a segment changes it.
         */
        Snapshot run_dynamic_tiles(const Runtime& rt, const Script& script, Drive how)
        {
            Tiles tiles(rt);
            Snapshot shot;

            auto world = rt.dynamic_graph();
            record_kept_regions(world, tiles, script);

            for (const Segment& segment : script.segments)
            {
                shape_mix_region(world, tiles, segment);
                for (std::size_t r = 0; r < segment.runs; ++r)
                    drive(world, how, K_STEPS);
            }

            take_tiles(shot, tiles);
            return shot;
        }

        /** @brief Holds what a DynamicGraph leaves after a script: its outputs and the layer of every run. */
        struct LayeredSnapshot
        {
            Snapshot shot;                      /**< The outputs, copied out after the last run. */
            std::vector<ExecutionLayer> layers; /**< The layer each run() call's RunReport names, in order. */
        };

        /**
         * @brief Runs a script on a DynamicGraph that allows the chain, one run(K_STEPS) per run of a segment.
         *
         * Every segment composes the plan again, so each run reads the layer of a fresh classification.
         */
        LayeredSnapshot run_chained_dynamic_tiles(const Runtime& rt, const Script& script)
        {
            Tiles tiles(rt);
            LayeredSnapshot result;

            auto world = rt.dynamic_graph();
            world.allow_chain(true);
            record_kept_regions(world, tiles, script);

            for (const Segment& segment : script.segments)
            {
                shape_mix_region(world, tiles, segment);
                for (std::size_t r = 0; r < segment.runs; ++r)
                    result.layers.push_back(world.run(K_STEPS).value().layer);
            }

            take_tiles(result.shot, tiles);
            return result;
        }

        /** @brief Returns the layer of a run on the runtime of a row: the scheduler when forced, the chain otherwise. */
        ExecutionLayer layer_of_row(bool force_scheduler) noexcept
        {
            return force_scheduler ? ExecutionLayer::Scheduler : ExecutionLayer::Chain;
        }

        /** @brief Folds one buffer into another, element by element. */
        struct MixPass
        {
            const Word* from; /**< K_HOST_N elements read. */
            Word* into;       /**< K_HOST_N elements rewritten. */

            /** @brief Runs the whole pass on the calling thread or work-item. */
            void apply() const noexcept
            {
                for (std::size_t i = 0; i < K_HOST_N; ++i)
                    into[i] = (into[i] ^ (from[i] * 2654435761u)) + 7u;
            }
        };

        /** @brief Folds a running value over one buffer into another, in index order. */
        struct ScanPass
        {
            const Word* from; /**< K_HOST_N elements read in index order. */
            Word* into;       /**< K_HOST_N elements rewritten. */

            /** @brief Runs the whole pass on the calling thread or work-item. */
            void apply() const noexcept
            {
                Word running = 0u;
                for (std::size_t i = 0; i < K_HOST_N; ++i)
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
        void record_pass(Graph& g, const Reads& reads, const Writes& writes,
                         Pass pass, PassSite site)
        {
            if (site == PassSite::Host)
            {
                g.add_host(reads, writes, [pass] { pass.apply(); });
                return;
            }
            g.add(std::size_t{1}, reads, writes, [pass](std::size_t) { pass.apply(); });
        }

        /**
         * @brief Runs a five-task dependency chain in which two passes sit between device kernels.
         * @param site Where the two passes run; the three kernels always run on the device.
         */
        Snapshot run_pass_chain(const Runtime& rt, PassSite site)
        {
            auto a = rt.buffer<Word>(K_HOST_N).value();
            auto b = rt.buffer<Word>(K_HOST_N).value();
            auto c = rt.buffer<Word>(K_HOST_N).value();
            auto d = rt.buffer<Word>(K_HOST_N).value();
            for (std::size_t i = 0; i < K_HOST_N; ++i)
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
            g.add(K_HOST_N, Reads(a), Writes(a),
                  [pa](std::size_t i) { pa[i] = pa[i] * 31u + static_cast<Word>(i) + 1u; });
            record_pass(g, Reads(a), Writes(b), MixPass{pa, pb}, site);
            g.add(K_HOST_N, Reads(b), Writes(c),
                  [pb, pc](std::size_t i) { pc[i] = pc[i] * 17u + pb[i]; });
            record_pass(g, Reads(c), Writes(d), ScanPass{pc, pd}, site);
            g.add(K_HOST_N, Reads(a, d), Writes(a),
                  [pa, pd](std::size_t i) { pa[i] += pd[i]; });

            for (std::size_t r = 0; r < K_RUNS; ++r)
                drive(g, Drive::Blocking, K_STEPS);

            Snapshot shot;
            take(shot, "a", Image(pa, pa + K_HOST_N));
            take(shot, "b", Image(pb, pb + K_HOST_N));
            take(shot, "c", Image(pc, pc + K_HOST_N));
            take(shot, "d", Image(pd, pd + K_HOST_N));
            return shot;
        }

        /** @brief Returns the bytes the tile workload leaves, computed on the host in the order the kernels are recorded. */
        Snapshot tile_reference(const Script& script)
        {
            Image scale(K_TILE_N);
            Image mix(K_TILE_N);
            Image fold(K_TILE_N);
            Image field(K_TILE_N);
            for (std::size_t i = 0; i < K_TILE_N; ++i)
            {
                scale[i] = static_cast<Word>(i) * 3u + 1u;
                mix[i]   = static_cast<Word>(i) ^ 0x9E3779B9u;
                fold[i]  = 0u;
                field[i] = static_cast<Word>(i) + 11u;
            }

            for (const Segment& segment : script.segments)
                for (std::size_t step = 0; step < segment.runs * K_STEPS; ++step)
                {
                    for (std::size_t i = 0; i < K_TILE_N; ++i)
                        scale[i] = scale[i] * 31u + static_cast<Word>(i) + 1u;
                    if (segment.mix_live)
                        for (std::size_t i = 0; i < K_TILE_N; ++i)
                            mix[i] = (mix[i] ^ (scale[i] * 2654435761u)) + 7u;
                    for (std::size_t i = 0; i < K_TILE_N; ++i)
                        fold[i] = fold[i] * 17u + scale[i] + mix[i];
                    if (script.evolve_field)
                        for (std::size_t i = 0; i < K_TILE_N; ++i)
                            field[i] = field[i] * 5u + static_cast<Word>(i % 7);
                }

            Snapshot shot;
            take(shot, "scale", scale);
            take(shot, "mix", mix);
            take(shot, "fold", fold);
            take(shot, "field", field);
            return shot;
        }

        /** @brief Returns the bytes the pass chain leaves after K_RUNS runs of K_STEPS steps, computed on the host. */
        Snapshot pass_chain_reference()
        {
            Image a(K_HOST_N);
            Image b(K_HOST_N);
            Image c(K_HOST_N);
            Image d(K_HOST_N);
            for (std::size_t i = 0; i < K_HOST_N; ++i)
            {
                a[i] = static_cast<Word>(i) * 7u + 3u;
                b[i] = static_cast<Word>(i) ^ 0x85EBCA6Bu;
                c[i] = 1u;
                d[i] = 0u;
            }

            for (std::size_t step = 0; step < K_RUNS * K_STEPS; ++step)
            {
                for (std::size_t i = 0; i < K_HOST_N; ++i)
                    a[i] = a[i] * 31u + static_cast<Word>(i) + 1u;
                MixPass{a.data(), b.data()}.apply();
                for (std::size_t i = 0; i < K_HOST_N; ++i)
                    c[i] = c[i] * 17u + b[i];
                ScanPass{c.data(), d.data()}.apply();
                for (std::size_t i = 0; i < K_HOST_N; ++i)
                    a[i] += d[i];
            }

            Snapshot shot;
            take(shot, "a", a);
            take(shot, "b", b);
            take(shot, "c", c);
            take(shot, "d", d);
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
            const std::size_t cell = row * K_GRID_COLS + col;
            out[cell] = stamp(out[cell], in[cell], cell);
        }

        /** @brief Returns the launch box given by a range and an origin. */
        Extent<2> box_by_origin()
        {
            return Extent<2>(sycl::range<2>(K_BOX_ROWS, K_BOX_COLS), sycl::id<2>(K_BOX_ROW0, K_BOX_COL0));
        }

        /** @brief Returns the same launch box given by sizes and rebased with at(). */
        Extent<2> box_by_rebase()
        {
            return Extent{K_BOX_ROWS, K_BOX_COLS}.at(sycl::id<2>(K_BOX_ROW0, K_BOX_COL0));
        }

        /** @brief Returns the box-sized tiled launch; NdRange carries no origin. */
        NdRange<2> box_tiles()
        {
            return NdRange{sycl::range<2>(K_BOX_ROWS, K_BOX_COLS), sycl::range<2>(K_TILE_ROWS, K_TILE_COLS)};
        }

        /** @brief Returns the grid a host loop reaches after @p steps steps over the box. */
        Image reference_grid(std::size_t steps)
        {
            Image source(K_GRID_N);
            Image grid(K_GRID_N);
            for (std::size_t k = 0; k < K_GRID_N; ++k)
            {
                source[k] = source_word(k);
                grid[k]   = seed_word(k);
            }
            for (std::size_t s = 0; s < steps; ++s)
                for (std::size_t row = K_BOX_ROW0; row < K_BOX_ROW0 + K_BOX_ROWS; ++row)
                    for (std::size_t col = K_BOX_COL0; col < K_BOX_COL0 + K_BOX_COLS; ++col)
                        stamp_cell(source.data(), grid.data(), row, col);
            return grid;
        }

        /** @brief Returns the box cells of a grid, packed row by row. */
        Image box_of(const Image& grid)
        {
            Image box(K_BOX_N);
            for (std::size_t row = 0; row < K_BOX_ROWS; ++row)
                for (std::size_t col = 0; col < K_BOX_COLS; ++col)
                    box[row * K_BOX_COLS + col] = grid[(row + K_BOX_ROW0) * K_GRID_COLS + col + K_BOX_COL0];
            return box;
        }

        /** @brief Allocates a grid-sized buffer on a device of @p rt holding the starting words. */
        Buffer<Word> seeded_grid(const Runtime& rt, DeviceIndex device = DeviceIndex{0})
        {
            auto grid = rt.buffer<Word>(K_GRID_N, device).value();
            for (std::size_t k = 0; k < K_GRID_N; ++k)
                grid[k] = seed_word(k);
            return grid;
        }

        /** @brief Allocates the read-only input grid on a device of @p rt. */
        Buffer<Word> source_grid(const Runtime& rt, DeviceIndex device = DeviceIndex{0})
        {
            auto grid = rt.buffer<Word>(K_GRID_N, device).value();
            for (std::size_t k = 0; k < K_GRID_N; ++k)
                grid[k] = source_word(k);
            return grid;
        }

        /**
         * @brief Runs the offset box through every tracked launch form, each writing its own output.
         *
         * The State is box-sized: its slot is the cell's position inside the box.
         */
        Snapshot run_tracked_launches(const Runtime& rt)
        {
            auto source     = source_grid(rt);
            auto by_origin  = seeded_grid(rt);
            auto by_rebase  = seeded_grid(rt);
            auto by_capture = seeded_grid(rt);
            auto by_tiles   = seeded_grid(rt);
            auto field      = rt.state<Word>(K_BOX_N).value();

            const Image seeded_box = box_of(reference_grid(0));
            for (std::size_t slot = 0; slot < K_BOX_N; ++slot)
                field[slot] = seeded_box[slot];

            const Word* in = source.data();
            Word* origin_out = by_origin.data();
            Word* rebase_out = by_rebase.data();
            Word* tiles_out  = by_tiles.data();

            auto g = rt.graph();
            g.add(box_by_origin(), Reads(source, by_origin), Writes(by_origin),
                  [in, origin_out](sycl::id<2> idx) { stamp_cell(in, origin_out, idx[0], idx[1]); });
            g.add(box_by_rebase(), Reads(source, by_rebase), Writes(by_rebase),
                  [in, rebase_out](sycl::id<2> idx) { stamp_cell(in, rebase_out, idx[0], idx[1]); });
            g.add(box_by_origin(), In(source), InOut(by_capture),
                  [](sycl::id<2> idx, const Word* captured_in, Word* captured_out)
                  {
                      stamp_cell(captured_in, captured_out, idx[0], idx[1]);
                  });
            g.add_rule(box_by_origin(), field, [in](sycl::id<2> idx, const Word* cur, Word* next)
            {
                const std::size_t cell = idx[0] * K_GRID_COLS + idx[1];
                const std::size_t slot = (idx[0] - K_BOX_ROW0) * K_BOX_COLS + (idx[1] - K_BOX_COL0);
                next[slot] = stamp(cur[slot], in[cell], cell);
            });
            g.add(box_tiles(), Reads(source, by_tiles), Writes(by_tiles),
                  [in, tiles_out](sycl::nd_item<2> item)
                  {
                      stamp_cell(in, tiles_out, item.get_global_id(0) + K_BOX_ROW0,
                                 item.get_global_id(1) + K_BOX_COL0);
                  });

            drive(g, Drive::Blocking, K_STEPS);

            Snapshot shot;
            take(shot, "Extent(range, origin)", Image(origin_out, origin_out + K_GRID_N));
            take(shot, "Extent{sizes}.at(origin)", Image(rebase_out, rebase_out + K_GRID_N));
            take(shot, "Extent with In/InOut", Image(by_capture.data(), by_capture.data() + K_GRID_N));
            take(shot, "NdRange", Image(tiles_out, tiles_out + K_GRID_N));
            take(shot, "State over Extent", Image(field.current(), field.current() + K_BOX_N));
            return shot;
        }

        /** @brief Returns what run_tracked_launches() must produce, computed on the host. */
        Snapshot tracked_launch_reference()
        {
            const Image grid = reference_grid(K_STEPS);
            Snapshot shot;
            take(shot, "Extent(range, origin)", grid);
            take(shot, "Extent{sizes}.at(origin)", grid);
            take(shot, "Extent with In/InOut", grid);
            take(shot, "NdRange", grid);
            take(shot, "State over Extent", box_of(grid));
            return shot;
        }

        /** @brief Runs the offset box through both untracked launch forms, one output each. */
        Snapshot run_untracked_launches(const Runtime& rt)
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
                stamp_cell(in, tiles_out, item.get_global_id(0) + K_BOX_ROW0,
                           item.get_global_id(1) + K_BOX_COL0);
            });

            drive(g, Drive::Blocking, K_STEPS);

            Snapshot shot;
            take(shot, "untracked Extent", Image(extent_out, extent_out + K_GRID_N));
            take(shot, "untracked NdRange", Image(tiles_out, tiles_out + K_GRID_N));
            return shot;
        }

        /**
         * @brief Runs both untracked launch forms on the runtime's last device, placed with On().
         *
         * Every buffer lives on that device, so a kernel routed anywhere else faults or misses.
         */
        Snapshot run_placed_untracked_launches(const Runtime& rt)
        {
            const DeviceIndex device{rt.advanced().device_count() - 1};
            auto source    = source_grid(rt, device);
            auto by_extent = seeded_grid(rt, device);
            auto by_tiles  = seeded_grid(rt, device);

            const Word* in = source.data();
            Word* extent_out = by_extent.data();
            Word* tiles_out  = by_tiles.data();

            // The two kernels are unordered; each is the only writer of its own buffer.
            auto g = rt.graph();
            g.add_untracked(box_by_origin(), On(by_extent), [in, extent_out](sycl::id<2> idx)
            {
                stamp_cell(in, extent_out, idx[0], idx[1]);
            });
            g.add_untracked(box_tiles(), On(device), [in, tiles_out](sycl::nd_item<2> item)
            {
                stamp_cell(in, tiles_out, item.get_global_id(0) + K_BOX_ROW0,
                           item.get_global_id(1) + K_BOX_COL0);
            });

            drive(g, Drive::Blocking, K_STEPS);

            Snapshot shot;
            take(shot, "untracked Extent", Image(extent_out, extent_out + K_GRID_N));
            take(shot, "untracked NdRange", Image(tiles_out, tiles_out + K_GRID_N));
            return shot;
        }

        /** @brief Returns what run_untracked_launches() must produce, computed on the host. */
        Snapshot untracked_launch_reference()
        {
            const Image grid = reference_grid(K_STEPS);
            Snapshot shot;
            take(shot, "untracked Extent", grid);
            take(shot, "untracked NdRange", grid);
            return shot;
        }

        constexpr std::size_t K_LANES      = 4;  /**< Lanes of the lane workload, one buffer each. */
        constexpr std::size_t K_LEVELS     = 12; /**< Nodes per lane. */
        constexpr std::size_t K_LANE_N     = 64; /**< Elements per lane buffer. */
        constexpr std::size_t K_CROSS_EVERY = 4; /**< Every fourth level also reads the next lane. */
        constexpr std::size_t K_LANE_NODES = K_LANES * K_LEVELS; /**< Nodes of the lane workload. */

        /** @brief Returns whether the node at @p level reads the next lane as well as its own. */
        constexpr bool crosses(std::size_t level) noexcept
        {
            return level != 0 && level % K_CROSS_EVERY == 0;
        }

        /** @brief Returns the next value of an element from its value, the next lane's value and the node. */
        constexpr Word lane_mix(Word own, Word next, std::size_t node) noexcept
        {
            return own * 1664525u + (next ^ static_cast<Word>(node)) + 1013904223u;
        }

        /** @brief Returns the starting word of element @p i of lane @p lane. */
        constexpr Word lane_seed(std::size_t lane, std::size_t i) noexcept
        {
            return static_cast<Word>(lane * K_LANE_N + i) * 2654435761u + 3u;
        }

        /**
         * @brief Records the node at @p level of @p lane: it rewrites its lane and, on a crossing level, reads the next.
         *
         * Lane l of level d is node d * K_LANES + l, recorded in that order, so the next lane is read as the
         * nodes recorded before this one left it.
         */
        void record_lane_node(Graph& g, std::vector<Buffer<Word>>& lanes, std::size_t level, std::size_t lane)
        {
            const std::size_t node = level * K_LANES + lane;
            Buffer<Word>& own = lanes[lane];
            Buffer<Word>& next = lanes[(lane + 1) % K_LANES];
            Word* own_data = own.data();
            const Word* next_data = next.data();
            if (!crosses(level))
            {
                g.add(K_LANE_N, Reads(own), Writes(own),
                      [own_data, node](std::size_t i) { own_data[i] = lane_mix(own_data[i], 0u, node); });
                return;
            }
            g.add(K_LANE_N, Reads(own, next), Writes(own),
                  [own_data, next_data, node](std::size_t i)
                  {
                      own_data[i] = lane_mix(own_data[i], next_data[i], node);
                  });
        }

        /** @brief Allocates the lane buffers on @p rt, each holding its starting words. */
        std::vector<Buffer<Word>> seeded_lanes(const Runtime& rt)
        {
            std::vector<Buffer<Word>> lanes;
            lanes.reserve(K_LANES);
            for (std::size_t lane = 0; lane < K_LANES; ++lane)
            {
                lanes.push_back(rt.buffer<Word>(K_LANE_N).value());
                for (std::size_t i = 0; i < K_LANE_N; ++i)
                    lanes.back()[i] = lane_seed(lane, i);
            }
            return lanes;
        }

        /** @brief Records the whole lane workload, level by level and lane by lane. */
        void record_lanes(Graph& g, std::vector<Buffer<Word>>& lanes)
        {
            for (std::size_t level = 0; level < K_LEVELS; ++level)
                for (std::size_t lane = 0; lane < K_LANES; ++lane)
                    record_lane_node(g, lanes, level, lane);
        }

        /** @brief Copies every lane into a snapshot. */
        Snapshot take_lanes(const std::vector<Buffer<Word>>& lanes)
        {
            static const char* const K_NAMES[K_LANES] = {"lane 0", "lane 1", "lane 2", "lane 3"};
            Snapshot shot;
            for (std::size_t lane = 0; lane < K_LANES; ++lane)
                take(shot, K_NAMES[lane], Image(lanes[lane].data(), lanes[lane].data() + K_LANE_N));
            return shot;
        }

        /**
         * @brief Runs the lane workload for K_STEPS steps on @p rt and copies the lanes out.
         * @param timed Whether an observer is added for the run, which makes the run record device timing.
         */
        Snapshot run_lanes(const Runtime& rt, bool timed)
        {
            std::vector<Buffer<Word>> lanes = seeded_lanes(rt);
            std::optional<Testing::RunRecorder> recorder;
            if (timed)
                recorder.emplace(rt);
            auto g = rt.graph();
            record_lanes(g, lanes);

            drive(g, Drive::Blocking, K_STEPS);

            return take_lanes(lanes);
        }

        /** @brief Returns the lanes the workload leaves after K_STEPS steps, computed on the host in recorded order. */
        Snapshot lane_reference()
        {
            std::vector<Image> lanes(K_LANES, Image(K_LANE_N));
            for (std::size_t lane = 0; lane < K_LANES; ++lane)
                for (std::size_t i = 0; i < K_LANE_N; ++i)
                    lanes[lane][i] = lane_seed(lane, i);

            for (std::size_t step = 0; step < K_STEPS; ++step)
                for (std::size_t level = 0; level < K_LEVELS; ++level)
                    for (std::size_t lane = 0; lane < K_LANES; ++lane)
                    {
                        const std::size_t node = level * K_LANES + lane;
                        const Image& next = lanes[(lane + 1) % K_LANES];
                        for (std::size_t i = 0; i < K_LANE_N; ++i)
                            lanes[lane][i] = lane_mix(lanes[lane][i], crosses(level) ? next[i] : 0u, node);
                    }

            static const char* const K_NAMES[K_LANES] = {"lane 0", "lane 1", "lane 2", "lane 3"};
            Snapshot shot;
            for (std::size_t lane = 0; lane < K_LANES; ++lane)
                take(shot, K_NAMES[lane], lanes[lane]);
            return shot;
        }

        /** @brief The level whose nodes the task-body variant of the lane workload records as task bodies. */
        constexpr std::size_t K_BODY_LEVEL = 6;
        static_assert(!crosses(K_BODY_LEVEL), "a task-body node rewrites its own lane only");

        /**
         * @brief Records the node at K_BODY_LEVEL of @p lane as a task body that submits the node's kernel itself.
         * @param threads Notes the thread each body runs on; must outlive the graph's runs.
         */
        void record_lane_body(Graph& g, std::vector<Buffer<Word>>& lanes, std::size_t lane,
                              TestSupport::SubmittingThreads& threads)
        {
            const std::size_t node = K_BODY_LEVEL * K_LANES + lane;
            Buffer<Word>& own = lanes[lane];
            Word* own_data = own.data();
            g.add_task(Reads(own), Writes(own),
                       [own_data, node, &threads](sycl::queue& queue, const std::vector<sycl::event>& dependencies)
                       {
                           threads.note();
                           return queue.submit([&](sycl::handler& handler)
                           {
                               handler.depends_on(dependencies);
                               handler.parallel_for(sycl::range<1>(K_LANE_N), [own_data, node](sycl::id<1> i)
                               {
                                   own_data[i[0]] = lane_mix(own_data[i[0]], 0u, node);
                               });
                           });
                       });
        }

        /** @brief Runs the lane workload with K_BODY_LEVEL as task bodies for K_STEPS steps and copies the lanes out. */
        Snapshot run_lanes_with_bodies(const Runtime& rt, bool timed, TestSupport::SubmittingThreads& threads)
        {
            std::vector<Buffer<Word>> lanes = seeded_lanes(rt);
            std::optional<Testing::RunRecorder> recorder;
            if (timed)
                recorder.emplace(rt);
            auto g = rt.graph();
            for (std::size_t level = 0; level < K_LEVELS; ++level)
                for (std::size_t lane = 0; lane < K_LANES; ++lane)
                {
                    if (level == K_BODY_LEVEL)
                        record_lane_body(g, lanes, lane, threads);
                    else
                        record_lane_node(g, lanes, level, lane);
                }

            drive(g, Drive::Blocking, K_STEPS);

            return take_lanes(lanes);
        }

        /** @brief Returns the program's runtime that keeps every run on the scheduler, with or without the funnel. */
        Runtime& scheduler_runtime(bool submit_funnel)
        {
            TestSupport::RuntimeShape shape;
            shape.force_scheduler = true;
            shape.submit_funnel = submit_funnel;
            return TestSupport::RuntimeEnvironment::runtime(shape);
        }

        /** @brief Returns how many node records the one observed run of the lane workload left on @p rt. */
        std::size_t lane_records(const Runtime& rt)
        {
            std::vector<Buffer<Word>> lanes = seeded_lanes(rt);
            Testing::RunRecorder recorder(rt);
            auto g = rt.graph();
            record_lanes(g, lanes);

            drive(g, Drive::Blocking, K_STEPS);

            return recorder.count() == 1 ? recorder.last().events.size() : 0;
        }

        /** @brief Names a runtime shape and whether a run on it says it was funnelled. */
        struct FunnelReportRow
        {
            const char* name;     /**< The row's label. */
            bool force_scheduler; /**< Whether the runtime keeps every run on the scheduler. */
            bool submit_funnel;   /**< Whether the runtime has the funnel. */
            bool funnelled;       /**< What RunReport::funnelled must say. */
        };

        /** @brief Prints a row by its label, so a failing row names itself. */
        void PrintTo(const FunnelReportRow& row, std::ostream* os)
        {
            *os << row.name;
        }

        /** @brief Returns what RunReport::funnelled says of one run of a one-kernel graph on the runtime of @p row. */
        bool funnelled_on(const FunnelReportRow& row)
        {
            TestSupport::RuntimeShape shape;
            shape.force_scheduler = row.force_scheduler;
            shape.submit_funnel = row.submit_funnel;
            const Runtime& rt = TestSupport::RuntimeEnvironment::runtime(shape);
            auto buf = rt.buffer<Word>(K_LANE_N).value();
            Word* p = buf.data();
            auto g = rt.graph();
            g.add(K_LANE_N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<Word>(i); });
            return g.run().value().funnelled;
        }

        /** @brief Owns the data of the task-pair workload: two outputs and a source both tasks may read. */
        struct TaskPair
        {
            /** @brief Allocates the data on @p rt and writes the starting values. */
            explicit TaskPair(const Runtime& rt)
                : left(rt.buffer<Word>(K_TILE_N).value()),
                  right(rt.buffer<Word>(K_TILE_N).value()),
                  source(rt.buffer<Word>(K_TILE_N).value())
            {
                for (std::size_t i = 0; i < K_TILE_N; ++i)
                {
                    left[i]   = static_cast<Word>(i) * 7u + 2u;
                    right[i]  = static_cast<Word>(i) ^ 0x5BD1E995u;
                    source[i] = static_cast<Word>(i) * 13u + 5u;
                }
            }

            Buffer<Word> left;   /**< Rewritten by the first task body. */
            Buffer<Word> right;  /**< Rewritten by the second task body. */
            Buffer<Word> source; /**< Read by both bodies in the shared-read form, by neither otherwise. */
        };

        /**
         * @brief Records a task body that submits one kernel rewriting @p out, reading @p from when given.
         * @param from Elements read, or nullptr; must be the data of the buffer @p reads names.
         */
        void record_task_body(Region& region, const Reads& reads, Buffer<Word>& out, const Word* from, Word salt)
        {
            Word* into = out.data();
            region.add_task(reads, Writes(out),
                            [into, from, salt](sycl::queue& queue, const std::vector<sycl::event>& after)
                            {
                                return queue.submit([&](sycl::handler& handler)
                                {
                                    handler.depends_on(after);
                                    handler.parallel_for(sycl::range<1>(K_TILE_N), [into, from, salt](sycl::id<1> id)
                                    {
                                        const std::size_t i = id[0];
                                        const Word read = (from != nullptr) ? from[i] : 0u;
                                        into[i] = into[i] * 29u + read + salt + static_cast<Word>(i);
                                    });
                                });
                            });
        }

        /**
         * @brief Runs two task-body regions the plan leaves unordered on a DynamicGraph allowed to chain,
         *        K_RUNS runs of K_STEPS steps.
         * @param share_source True to make both bodies read the source buffer; false for disjoint data.
         */
        LayeredSnapshot run_task_pair(const Runtime& rt, bool share_source)
        {
            TaskPair data(rt);
            LayeredSnapshot result;

            auto world = rt.dynamic_graph();
            world.allow_chain(true);
            const Word* from = share_source ? data.source.data() : nullptr;
            const Reads reads = share_source ? Reads(data.source) : Reads{};
            record_task_body(world.region(K_KEY_SCALE), reads, data.left, from, 3u);
            record_task_body(world.region(K_KEY_MIX), reads, data.right, from, 5u);

            for (std::size_t r = 0; r < K_RUNS; ++r)
                result.layers.push_back(world.run(K_STEPS).value().layer);

            take(result.shot, "left", Image(data.left.data(), data.left.data() + K_TILE_N));
            take(result.shot, "right", Image(data.right.data(), data.right.data() + K_TILE_N));
            return result;
        }
    } // namespace
} // namespace

/** @brief Fixture of the equivalence tests that need no device, which run on a host runtime. */
class Small_LayerEquivalence : public TestSupport::HostRuntimeTest
{
};

/** @brief Fixture of the rows that run a workload on the in-order chain (false) and on the scheduler (true). */
class Medium_LayerEquivalence : public ::testing::TestWithParam<bool>, protected TestSupport::LayerRuntime
{
    protected:
        /** @brief Chooses the runtime of the row. */
        void SetUp() override { use_layer(GetParam()); }
};

/** @brief Fixture of the tests of DynamicGraph::allow_chain() on the runtime that is not forced onto the scheduler. */
class Medium_DynamicGraphChain : public TestSupport::RuntimeTest
{
};

/** @brief Fixture of the rows of the lane workload run untimed (false) and timed (true), funnel off against on. */
class Medium_SubmitFunnelOutputs : public ::testing::TestWithParam<bool>
{
};

/** @brief Fixture of the rows that run on the scheduler without (false) and with (true) the submit funnel. */
class Medium_SubmitFunnel : public ::testing::TestWithParam<bool>
{
};

/** @brief Fixture of the rows of runtime shapes and what RunReport::funnelled says of a run on each. */
class Medium_SubmitFunnelReport : public ::testing::TestWithParam<LayerEquivalenceTests::FunnelReportRow>
{
};

/** @brief Fixture of the rows of the pass chain: where its two whole-buffer passes run, on a host runtime. */
class Small_LayerEquivalencePasses : public TestSupport::HostRuntimeTest,
                                     public ::testing::WithParamInterface<LayerEquivalenceTests::PassSite>
{
};

namespace LayerEquivalenceTests
{
    /** @brief Tests that a static graph and a DynamicGraph of four regions both leave the bytes of the host model. */
    TEST_F(Small_LayerEquivalence, DynamicRegionsMatchTheHostModel)
    {
        const Script script{{{true, K_RUNS}}, true};
        const Snapshot reference = tile_reference(script);

        expect_identical(reference, run_static_tiles(runtime(), script, Drive::Blocking),
                         "host model vs static graph");
        expect_identical(reference, run_dynamic_tiles(runtime(), script, Drive::Blocking),
                         "host model vs dynamic graph");
    }

    /** @brief Tests that a region dropped and recorded again leaves the bytes of the host model. */
    TEST_F(Small_LayerEquivalence, DroppedAndReaddedRegionMatchesTheHostModel)
    {
        const Script script{{{true, 1}, {false, 1}, {true, 1}}, false};

        expect_identical(tile_reference(script), run_dynamic_tiles(runtime(), script, Drive::Blocking),
                         "host model vs dynamic graph");
    }

    /** @brief Tests that the pass chain leaves the bytes of the host model, with the passes on the host or on the device. */
    TEST_P(Small_LayerEquivalencePasses, PassChainMatchesTheHostModel)
    {
        expect_identical(pass_chain_reference(), run_pass_chain(runtime(), GetParam()),
                         "host model vs pass chain");
    }

    /** @brief Tests that N run_async() steps of a static graph leave the bytes of the host model. */
    TEST_F(Small_LayerEquivalence, AsyncStepsMatchTheHostModel)
    {
        const Script script{{{true, K_RUNS}}, true};

        expect_identical(tile_reference(script), run_static_tiles(runtime(), script, Drive::Asynchronous),
                         "host model vs run_async");
    }

    /** @brief Tests that a DynamicGraph stepped through run_async() leaves the bytes of the host model. */
    TEST_F(Small_LayerEquivalence, AsyncStepsOfADynamicGraphMatchTheHostModel)
    {
        const Script script{{{true, 1}, {false, 1}, {true, 1}}, true};

        expect_identical(tile_reference(script), run_dynamic_tiles(runtime(), script, Drive::Asynchronous),
                         "host model vs dynamic run_async");
    }

    /** @brief Tests that every tracked form of an offset 2-D launch matches the host reference. */
    TEST_P(Medium_LayerEquivalence, OffsetLaunchFormsMatchTheHostReference)
    {
        expect_identical(tracked_launch_reference(), run_tracked_launches(runtime()),
                         "host reference vs tracked launches");
    }

    /** @brief Tests that the untracked Extent and NdRange launches match the host reference. */
    TEST_P(Medium_LayerEquivalence, UntrackedOffsetLaunchesMatchTheHostReference)
    {
        expect_identical(untracked_launch_reference(), run_untracked_launches(runtime()),
                         "host reference vs untracked launches");
    }

    /** @brief Tests that an untracked kernel placed with On() runs on the device it names. */
    TEST_P(Medium_LayerEquivalence, PlacedUntrackedLaunchesMatchTheHostReference)
    {
        expect_identical(untracked_launch_reference(), run_placed_untracked_launches(runtime()),
                         "host reference vs placed untracked launches");
    }

    /** @brief Tests that a DynamicGraph allowed to chain, whose mix region is dropped and recorded again, leaves the bytes of the host model. */
    TEST_P(Medium_LayerEquivalence, ChainedDynamicRegionsMatchTheHostModel)
    {
        const Script script{{{true, 1}, {false, 1}, {true, 1}}, false};

        expect_identical(tile_reference(script), run_chained_dynamic_tiles(runtime(), script).shot,
                         "host model vs dynamic graph allowed to chain");
    }

    /** @brief Tests that every composition of a DynamicGraph allowed to chain runs on the layer of the row's runtime. */
    TEST_P(Medium_LayerEquivalence, ChainedDynamicRegionsRunOnTheLayerOfTheRow)
    {
        const Script script{{{true, 1}, {false, 1}, {true, 1}}, false};
        const std::vector<ExecutionLayer> expected(3, layer_of_row(GetParam()));

        EXPECT_EQ(run_chained_dynamic_tiles(runtime(), script).layers, expected);
    }

    /** @brief Tests that a DynamicGraph never allowed to chain runs on the scheduler. */
    TEST_F(Medium_DynamicGraphChain, AGraphNotAllowedToChainRunsOnTheScheduler)
    {
        Tiles tiles(runtime());
        auto world = runtime().dynamic_graph();
        record_kept_regions(world, tiles, Script{{}, false});

        EXPECT_EQ(world.run(K_STEPS).value().layer, ExecutionLayer::Scheduler);
    }

    /** @brief Tests that allowing the chain over a plan composed without it chains the next run. */
    TEST_F(Medium_DynamicGraphChain, AllowingTheChainOverAComposedPlanChainsTheNextRun)
    {
        Tiles tiles(runtime());
        auto world = runtime().dynamic_graph();
        record_kept_regions(world, tiles, Script{{}, false});
        world.run(K_STEPS).value();

        world.allow_chain(true);

        EXPECT_EQ(world.run(K_STEPS).value().layer, ExecutionLayer::Chain);
    }

    /** @brief Tests that a DynamicGraph allowed to chain whose regions hold native host work runs on the scheduler. */
    TEST_F(Medium_DynamicGraphChain, AChainedGraphWithAHostNodeRunsOnTheScheduler)
    {
        Tiles tiles(runtime());
        Word* fold = tiles.fold.data();
        auto world = runtime().dynamic_graph();
        world.allow_chain(true);
        record_scale(world.region(K_KEY_SCALE), tiles);
        world.region(K_KEY_FOLD).add_host(Reads(tiles.scale), Writes(tiles.fold),
                                          [fold] { fold[0] += 1u; });

        EXPECT_EQ(world.run(K_STEPS).value().layer, ExecutionLayer::Scheduler);
    }

    /**
     * @brief Tests that two task bodies the composed plan leaves unordered over disjoint data run as a chain
     *        and leave the bytes of the forced scheduler.
     */
    TEST_F(Medium_DynamicGraphChain, UnorderedTaskBodiesOverDisjointDataChainWithTheSchedulersBytes)
    {
        const LayeredSnapshot chained = run_task_pair(runtime(), false);
        const LayeredSnapshot scheduled = run_task_pair(scheduler_runtime(false), false);

        EXPECT_EQ(chained.layers, std::vector<ExecutionLayer>(K_RUNS, ExecutionLayer::Chain));
        EXPECT_EQ(scheduled.layers, std::vector<ExecutionLayer>(K_RUNS, ExecutionLayer::Scheduler));
        expect_identical(scheduled.shot, chained.shot, "forced scheduler vs chain");
    }

    /** @brief Tests that two task bodies the composed plan leaves unordered and that read one buffer run on the scheduler. */
    TEST_F(Medium_DynamicGraphChain, UnorderedTaskBodiesReadingOneBufferRunOnTheScheduler)
    {
        EXPECT_EQ(run_task_pair(runtime(), true).layers,
                  std::vector<ExecutionLayer>(K_RUNS, ExecutionLayer::Scheduler));
    }

    /** @brief Tests that a DynamicGraph moved from one allowed to chain keeps the setting. */
    TEST_F(Medium_DynamicGraphChain, AMovedGraphKeepsItsChainSetting)
    {
        Tiles tiles(runtime());
        auto world = runtime().dynamic_graph();
        world.allow_chain(true);
        DynamicGraph moved(std::move(world));
        record_kept_regions(moved, tiles, Script{{}, false});

        EXPECT_EQ(moved.run(K_STEPS).value().layer, ExecutionLayer::Chain);
    }

    /** @brief Tests that the lane workload leaves the same bytes on the scheduler with the funnel on as with it off. */
    TEST_P(Medium_SubmitFunnelOutputs, AFunnelledRunLeavesTheBytesOfTheRunWithoutIt)
    {
        const Snapshot without = run_lanes(scheduler_runtime(false), GetParam());
        const Snapshot with = run_lanes(scheduler_runtime(true), GetParam());

        expect_identical(lane_reference(), without, "host model vs funnel off");
        expect_identical(without, with, "funnel off vs funnel on");
    }

    /**
     * @brief Tests that the lane workload with a level of task bodies leaves the same bytes with the funnel on as
     *        with it off, and that with it on every body runs on the one funnel thread.
     */
    TEST_P(Medium_SubmitFunnelOutputs, TaskBodiesSubmitFromTheFunnelThreadAndLeaveTheSameBytes)
    {
        TestSupport::SubmittingThreads off_threads;
        TestSupport::SubmittingThreads on_threads;

        const Snapshot without = run_lanes_with_bodies(scheduler_runtime(false), GetParam(), off_threads);
        const Snapshot with = run_lanes_with_bodies(scheduler_runtime(true), GetParam(), on_threads);

        expect_identical(lane_reference(), without, "host model vs funnel off");
        expect_identical(without, with, "funnel off vs funnel on");
        EXPECT_EQ(on_threads.ids().size(), 1u) << "every task body runs on the device's one funnel thread";
    }

    /** @brief Tests that a timed run of the lane workload keeps one node record per node and step. */
    TEST_P(Medium_SubmitFunnel, ATimedRunKeepsOneRecordPerNode)
    {
        EXPECT_EQ(lane_records(scheduler_runtime(GetParam())), K_LANE_NODES * K_STEPS);
    }

    /** @brief Tests that RunReport::funnelled says whether a submit funnel submitted the run's kernel nodes. */
    TEST_P(Medium_SubmitFunnelReport, TheReportSaysWhetherTheRunWasFunnelled)
    {
        EXPECT_EQ(funnelled_on(GetParam()), GetParam().funnelled);
    }
} // namespace LayerEquivalenceTests

INSTANTIATE_TEST_SUITE_P(Rows, Medium_SubmitFunnelOutputs, ::testing::Values(false, true));

INSTANTIATE_TEST_SUITE_P(Rows, Medium_SubmitFunnel, ::testing::Values(false, true));

INSTANTIATE_TEST_SUITE_P(
    Rows, Medium_SubmitFunnelReport,
    ::testing::Values(LayerEquivalenceTests::FunnelReportRow{"scheduler without the funnel", true, false, false},
                      LayerEquivalenceTests::FunnelReportRow{"scheduler with the funnel", true, true, true},
                      LayerEquivalenceTests::FunnelReportRow{"chain on a runtime with the funnel", false, true, false}));

INSTANTIATE_TEST_SUITE_P(Rows, Medium_LayerEquivalence, ::testing::Values(false, true));

INSTANTIATE_TEST_SUITE_P(
    Rows, Small_LayerEquivalencePasses,
    ::testing::Values(LayerEquivalenceTests::PassSite::Host,
                      LayerEquivalenceTests::PassSite::Device));
