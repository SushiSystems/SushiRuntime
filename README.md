# SushiRuntime

**SushiRuntime** is a high-performance runtime library designed for task-based parallel programming and SYCL integration. It helps manage complex compute tasks across different hardware devices like CPUs and GPUs.

## Key Features
- **Task Graph**: Executes kernels and data transfers with automatic dependency tracking.
- **Topology-aware**: Uses HWLOC to map NUMA nodes and PCI addresses to SYCL devices for better performance.
- **USM Memory Management**: Low-latency data access using Unified Shared Memory (USM) pools.
- **Multi-backend Support**: Compatible with SYCL (Intel DPC++), CUDA, and standard host systems.
- **Lock-free Design**: Uses atomic operations for task scheduling and memory management to reduce overhead.

## Architectural Overview
SushiRuntime is built with performance and stability in mind. Here are the core components:
- **Core Library**: Provides essential tools like thread-safe logging, intrusive reference counting, and smart pointers (`sushi_ptr`).
- **Execution Layer**: The `RuntimeContext` acts as the brain, managing hardware discovery and command queues.
- **Graph Layer**: The `TaskGraph` analyzes your tasks to build an execution plan based on data access (reads/writes).
- **Scheduler**: A work-stealing scheduler balances the load across all available CPU cores.
- **Memory System**: Optimized allocators provide fast memory management for heterogeneous systems.

## Confidentiality
This software is proprietary information of Mustafa Garip & Sushi Systems. Unauthorized copying or use is prohibited.
