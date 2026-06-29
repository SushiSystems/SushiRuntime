/**************************************************************************/
/* coupled_fields_demo.cpp                                                */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// Two fields that evolve together, one reading the other. v counts up by one each
// step; u accumulates v. The coupled add(out, in, fn) overload reads the *previous*
// step's data for every input (a Jacobi update): each State advances only after the
// whole step's kernels have run, so writing one field never changes what another
// reads in the same step. That is what makes the result independent of the order
// the two rules were added.
//
// With v starting at 0 and incrementing each step, after STEPS steps:
//   v == STEPS,  u == 0 + 1 + ... + (STEPS-1) == STEPS*(STEPS-1)/2.

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

    // u_next = u_cur + v_cur. v_cur is the previous step's v, not the v_next the
    // rule above produces this step — the Jacobi guarantee.
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
