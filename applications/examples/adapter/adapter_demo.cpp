/**************************************************************************/
/* adapter_demo.cpp                                                       */
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
 * @file adapter_demo.cpp
 * @brief Runs a diffusion through the host's backend and checks it against a host reference.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "diffusion_backend.hpp"

namespace
{
    /** @brief Cells of the field. */
    constexpr std::size_t K_CELLS = 256;

    /** @brief Steps the field is advanced by. */
    constexpr std::size_t K_STEPS = 100;

    /** @brief Share of the difference to its neighbours a cell moves each step. */
    constexpr float K_RATE = 0.25f;

    /** @brief Largest absolute difference accepted between a device cell and a host cell. */
    constexpr float K_TOLERANCE = 1.0e-5f;

    /** @brief Prints a failed call in the host's terms and returns the exit code of a failure. */
    int report_failure(const char* call, const HostProgram::Status& status)
    {
        std::fprintf(stderr, "adapter_demo: %s failed with %s: %s\n", call,
                     HostProgram::to_string(status.code), status.message.c_str());
        return 1;
    }

    /** @brief Advances a field on the host by the rule the backend applies. */
    std::vector<float> advance_on_host(std::vector<float> field, std::size_t steps, float rate)
    {
        std::vector<float> next(field.size());
        for (std::size_t step = 0; step < steps; ++step)
        {
            for (std::size_t i = 0; i < field.size(); ++i)
            {
                const float left = (i == 0) ? field[i] : field[i - 1];
                const float right = (i == field.size() - 1) ? field[i] : field[i + 1];
                next[i] = field[i] + rate * (left - 2.0f * field[i] + right);
            }
            field.swap(next);
        }
        return field;
    }

    /** @brief Runs the diffusion through the backend; returns 0 when it matches the host. */
    int run_adapter_demo()
    {
        using namespace HostProgram;

        BackendHandle backend;
        Status status = create_backend(backend);
        if (!status.is_ok())
            return report_failure("create_backend", status);

        // With no field allocated the library refuses the run; the host reads its own code.
        const Status early = advance(*backend, 1);
        std::printf("adapter: advance before allocate_field -> %s: %s\n", to_string(early.code),
                    early.message.c_str());
        if (early.code != StatusCode::InvalidRequest)
            return 1;

        std::vector<float> initial(K_CELLS, 0.0f);
        initial[K_CELLS / 2] = 1.0f;

        status = allocate_field(*backend, initial, K_RATE);
        if (!status.is_ok())
            return report_failure("allocate_field", status);
        status = advance(*backend, K_STEPS);
        if (!status.is_ok())
            return report_failure("advance", status);

        std::vector<float> field;
        status = read_field(*backend, field);
        if (!status.is_ok())
            return report_failure("read_field", status);

        const std::vector<float> expected = advance_on_host(initial, K_STEPS, K_RATE);
        bool is_correct = field.size() == expected.size();
        double total = 0.0;
        for (std::size_t i = 0; is_correct && i < field.size(); ++i)
        {
            is_correct = std::fabs(field[i] - expected[i]) <= K_TOLERANCE;
            total += field[i];
        }

        std::printf("adapter: %zu cells, %zu steps, total heat = %.3f\n", K_CELLS, K_STEPS, total);
        std::printf("  result: %s\n", is_correct ? "CORRECT" : "WRONG");
        return is_correct ? 0 : 1;
    }
} // namespace

int main()
{
    return run_adapter_demo();
}
