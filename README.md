# SushiRuntime

**SushiRuntime** is a high-performance runtime library designed for task-based parallel programming and sycl integration. it helps manage complex compute tasks across different hardware devices like cpus and gpus.

## key features
- **task graph**: executes kernels and data transfers with automatic dependency tracking.
- **topology-aware**: uses hwloc to map numa nodes and pci addresses to sycl devices for better performance.
- **usm memory management**: low-latency data access using unified shared memory (usm) pools.
- **multi-backend support**: compatible with sycl (intel dpc++), cuda, and standard host systems.
- **lock-free design**: uses atomic operations for task scheduling and memory management to reduce overhead.

## architectural overview
SushiRuntime is built with performance and stability in mind. here are the core components:
- **core library**: provides essential tools like thread-safe logging, intrusive reference counting, and smart pointers (`sushi_ptr`).
- **execution layer**: the `RuntimeContext` acts as the brain, managing hardware discovery and command queues.
- **graph layer**: the `TaskGraph` analyzes your tasks to build an execution plan based on data access (reads/writes).
- **scheduler**: a work-stealing scheduler balances the load across all available cpu cores.
- **memory system**: optimized allocators provide fast memory management for heterogeneous systems.

## confidentiality
this software is proprietary information of Mustafa Garip & Sushi Systems. unauthorized copying or use is prohibited.
