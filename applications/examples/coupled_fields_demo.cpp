/**************************************************************************/
/* coupled_fields_demo.cpp                                                */
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
 * @file coupled_fields_demo.cpp
 * @brief Runs two coupled fields where one rule reads the previous step of the other.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <cstdio>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Runs the two coupled fields and checks u and v against their closed forms. */
    int run_coupled_fields()
    {
        constexpr std::size_t N     = 1024;
        constexpr std::size_t STEPS = 100;

        auto rt = Runtime::create();
        auto u  = rt.state<int>(N);
        auto v  = rt.state<int>(N);
        for (std::size_t i = 0; i < N; ++i) { u[i] = 0; v[i] = 0; }

        auto sim = rt.graph();

        // v_next = v_cur + 1.
        sim.add(v, [](std::size_t i, const int* vc, int* vn)
        {
            vn[i] = vc[i] + 1;
        });

        // v_cur is the previous step's v, not the v_next of the rule above; see README.md.
        sim.add(Coupled(u, v), [](std::size_t i, const int* uc, int* un, const int* vc)
        {
            un[i] = uc[i] + vc[i];
        });

        sim.run(STEPS);

        constexpr int expected_v = static_cast<int>(STEPS);
        constexpr int expected_u = static_cast<int>(STEPS * (STEPS - 1) / 2);

        std::printf("coupled fields: %zu steps, compiles = %zu\n", STEPS, sim.compile_count());
        std::printf("  v[0] = %d  (expected %d)\n", v[0], expected_v);
        std::printf("  u[0] = %d  (expected %d)\n", u[0], expected_u);

        const bool ok = (v[0] == expected_v) && (u[0] == expected_u);
        std::printf("  result: %s\n", ok ? "CORRECT" : "WRONG");
        return ok ? 0 : 1;
    }
} // namespace

int main()
{
    try
    {
        return run_coupled_fields();
    }
    catch (const SushiException& error)
    {
        std::fprintf(stderr, "coupled_fields: %s\n", error.what());
        return 1;
    }
}
