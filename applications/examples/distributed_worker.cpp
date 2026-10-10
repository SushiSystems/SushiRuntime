/**************************************************************************/
/* distributed_worker.cpp                                                 */
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
 * @file distributed_worker.cpp
 * @brief Runs a worker process that serves the kernels of the distributed demo.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/worker_main.hpp>

#include "distributed_kernels.hpp"

using namespace SushiRuntime;

int main(int argc, char** argv)
{
    auto kernels = make_sushi<Distributed::KernelRegistry>();
    DistributedDemo::register_kernels(*kernels);

    // run_worker() reads the flags, serves the master until a signal and returns the exit code.
    return Distributed::run_worker(argc, argv, kernels);
}
