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
 * @file distributed_worker.cpp
 * @brief Runs a worker process that serves the kernels of the distributed demo.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <SushiRuntime/distributed/distributed.hpp>

#include "distributed_kernels.hpp"

using namespace SushiRuntime;

int main(int argc, char** argv)
{
    auto kernels = make_sushi<Distributed::KernelRegistry>();
    DistributedDemo::register_kernels(*kernels);

    // run_worker() reads the flags, serves the master until a signal and returns the exit code.
    return Distributed::run_worker(argc, argv, kernels);
}
