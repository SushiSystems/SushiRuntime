# IDEA: Lightweight Distributed SushiRuntime Architecture (Task Offloading)

This document outlines the architectural design for making **SushiRuntime** a lightweight, dynamic, and distributed task-scheduling runtime without the heavyweight dependency of MPI.

---

## 1. Architectural Concept: Master-Worker Dynamic Offloading

Instead of spawning rigid symmetric processes (like MPI ranks), this architecture uses an asymmetric **Master-Worker** pattern. The master node controls the execution graph, tracks resources, and offloads individual tasks to stateless worker nodes over a lightweight network layer only when local resources are saturated or network execution yields a performance benefit.

```mermaid
graph TD
    subgraph Master Node (SushiRuntime Brain)
        G[Graph Scheduler] -->|1. Load Analysis| LB[Lightweight Load Balancer]
        LB -->|2. Local Execution| LGPU[Local SYCL Device]
        LB -->|3. Remote Offload| Net[Network Offloader]
    end
    
    subgraph Network Layer
        Net -->|TCP / Binary Packets| ZMQ[(ZeroMQ or Asio)]
    end

    subgraph Worker Nodes (Stateless Executors)
        ZMQ -->|4. Receive Task Payload| W1[Worker Agent 1]
        ZMQ -->|4. Receive Task Payload| W2[Worker Agent 2]
        
        W1 -->|5. Execute SYCL Kernel| GPU1[Worker GPU]
        W2 -->|5. Execute SYCL Kernel| GPU2[Worker GPU]
        
        GPU1 -->|6. Retrieve Results| W1
        GPU2 -->|6. Retrieve Results| W2
        
        W1 -->|7. Send Output Packets| Net
        W2 -->|7. Send Output Packets| Net
    end
```

---

## 2. Key Components

### A. Load Balancer & Scheduler (Master)
Integrates into the existing `sushiruntime::scheduler` to dynamically decide where each task in the DAG (Directed Acyclic Graph) should be executed:
- **Metrics Tracked:** Local task queue length, worker availability, worker GPU memory capacity, and historical network latency.
- **Heuristic:** Offloads tasks if:
  $$\text{Local Queue Wait Time} > \text{Network Latency} + \text{Data Transfer Time} + \text{Worker Execution Time}$$

### B. Network Layer (ZeroMQ / Asio)
- **ZeroMQ (`cppzmq`):** Ideal choice. Highly recommended as it is lightweight, serverless (no broker daemon), handles reconnection, buffers messages, and is extremely low-latency.
- **Asio (Header-only):** Excellent standalone socket library for raw TCP/UDP networking without external DLL dependencies.

### C. Task Payload Protocol (Serialization)
To offload a task, the Master packages it into a binary packet containing:
1. **Task Metadata:**
   - `task_id` (Unique identifier for graph tracking)
   - `kernel_id` (Identifier of the compiled SYCL kernel to run)
   - `argument_count`
2. **Buffer Dimensions & Data Types:** Size, shape, and types of inputs/outputs.
3. **Input Payload:** The actual raw binary data (tensors/buffers) to be processed.

*Tip: Use simple binary offsets for zero-copy deserialization or lightweight serialization libraries like FlatBuffers / Protocol Buffers if structured schemas are desired.*

### D. Worker Agent (Daemon)
A lightweight daemon running on worker machines. Its cycle is simple and stateless:
```cpp
while (running) {
    auto packet = receive_task_packet();
    
    // 1. Allocate local SYCL memory (USM or Buffer)
    auto input_device_mem = sycl::malloc_device(packet.size, sycl_queue);
    
    // 2. Copy input payload to device
    sycl_queue.memcpy(input_device_mem, packet.data, packet.size).wait();
    
    // 3. Launch the designated SYCL kernel
    launch_sycl_kernel(packet.kernel_id, input_device_mem, ...);
    sycl_queue.wait();
    
    // 4. Retrieve results and package back to Master
    auto output_packet = package_results(packet.task_id, output_device_mem);
    send_response(output_packet);
}
```

---

## 3. Recommended Implementation Steps

1. **Setup Socket Ping-Pong:**
   Implement a minimal test using ZeroMQ or Asio in C++ to transfer a raw `std::vector<float>` between a master process and a worker process.
2. **Design the Packet Structure:**
   Create a standard structure (`struct TaskPacket`) that can seamlessly serialize and deserialize raw float/int buffers.
3. **Integrate into the Scheduler:**
   Intercept the task dispatch phase in `sushiruntime::scheduler`. Instead of directly pushing to the local SYCL queue, check if a remote worker is free and delegate the task.
4. **Optimize with Pipelining:**
   Overlay communication and computation. While the Worker is executing task $N$, the Master should already be transmitting task $N+1$ to hide network overhead.
