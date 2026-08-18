/**************************************************************************/
/* SushiRuntime.h                                                         */
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

#pragma once

/**
 * @file SushiRuntime.h
 * @brief Master header for the SushiRuntime library.
 * 
 * Including this file provides access to all public modules of the runtime.
 */

// Core Infrastructure
#include "version.hpp"
#include "core/export.hpp"
#include "core/common.hpp"
#include "core/error.hpp"
#include "core/sushi_ptr.hpp"
#include "core/ref_counted.hpp"
#include "core/logger.hpp"

// Hardware Topology & Discovery
#include "topology/hw_discovery.hpp"
#include "topology/numa_mapper.hpp"
#include "topology/affinity.hpp"

// Memory Management
#include "memory/epoch_reclamation.hpp"
#include "memory/usm_registry.hpp"

// Execution & Context
#include "execution/execution_engine.hpp"
#include "execution/runtime_context.hpp"

// Task Graph & Scheduling
#include "graph/node.hpp"
#include "graph/task_graph.hpp"
#include "graph/dependency_tracker.hpp"

// Scheduler Components
#include "scheduler/task_scheduler.hpp"
#include "scheduler/task.hpp"
#include "scheduler/event_polling.hpp"
#include "scheduler/rebalancer_interfaces.hpp"

// Fluent v1 API — the surface most users program against. The single umbrella
// pulls in the data handles, launch geometry, data-flow vocabulary, the chainable
// builders (Graph / Region / DynamicGraph for streaming, mutating worlds), and the
// Runtime entry point.
#include "api/fluent.hpp"
