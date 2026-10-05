/****************************************************************/
/* coupled_fields_demo.cpp                                      */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file coupled_fields_demo.cpp
 * @brief Runs two coupled fields where one rule reads the previous step of the other.
 * @author Mustafa Garip
 * @see examples/README.md
 */

#include <cstdio>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

int main()
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

    // v_cur is the previous step's v, not the v_next of the rule above; see examples/README.md.
    sim.add(u, v, [](std::size_t i, const int* uc, int* un, const int* vc)
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
