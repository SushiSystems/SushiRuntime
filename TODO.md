Project Issues and Roadmap

[COMPLETED] Local Queue Selection

Problem: The scheduler always uses queue 0. It ignores the hardware topology.

Solution: Implement the missing round-robin/least-loaded strategy. Select a SYCL queue that is local to the NUMA node where the data is located.

[COMPLETED] Task Dependencies

Problem: Tasks are submitted with empty dependencies. This can cause race conditions.

Solution: Track SYCL events between tasks in the graph and pass them to the execution handler. Link sycl_deps from Node to the actual q.submit call.

[COMPLETED] 3. Worker Idle Behavior

Problem: Worker threads use 100% CPU even when there is no work because they only use yield().

Solution: Use a sleep mechanism or adaptive backoff (e.g., waiting on a condition variable) when idle.

[COMPLETED] 4. Deque Capacity & Safety

Problem: The task queue size is fixed at 64k. If it fills up, the program crashes. It also uses unsafe void**.

Solution: Change the deque to automatically resize. Replace void** with Task* or sushi_ptr<Task>.

[COMPLETED] Type Safety & Casting Hazard

Problem: EventPolling blindly casts Task* to Node*. If a non-graph task (like RangeTask) is used, it will cause a Segfault.

Solution: Add a type-check mechanism in Task class (e.g. virtual bool is_graph_node()) and check before casting.

[COMPLETED] 6. Work Stealing Strategy

Problem: Workers always try to steal from the same queues first. This causes high contention.

Solution: Use a random victim selection strategy to spread the load.

[COMPLETED] 7. Scheduler Lifecycle Management

Problem: TaskGraph::execute creates and destroys a new TaskScheduler every time, which is extremely expensive (thread creation/joining).

Solution: Move the TaskScheduler to RuntimeContext so it remains persistent across multiple graph executions.

[COMPLETED] Graph Completion Event

Problem: TaskGraph::execute returns an empty sycl::event. Users cannot wait for graph completion properly.

Solution: Return the actual completion event (from the sentinel node) back to the user.

[COMPLETED] 9. Dead Code & Memory Integration

Problem: memory_pool.cpp is an empty file. BitmaskMemoryPool is implemented in a header but never actually used in the project.

Solution: Remove empty source files. Integrate the BitmaskMemoryPool into the USMAllocator or RuntimeContext.

[COMPLETED] 10. Successor Memory Management

Problem: SuccessorNode uses manual new and delete in a lock-free list. This is risky and slow.

Solution: Use an object pool or a fixed-size buffer inside each Node to manage successors without heap allocations.

[COMPLETED] Kernel Assignment Bug

Problem: TaskGraph::add_task forgets to assign to node->work. As a result, all KERNEL tasks added through this API are empty.

Solution: Map the incoming work lambda to node->work correctly inside the add_task implementation.

[COMPLETED] 12. False Memory Dependencies

Problem: DependencyTracker::get_shard_idx uses a simple modulo. Collisions cause unrelated tasks to wait for each other.

Solution: Use a better hash function or a dynamic mapping system to reduce false dependency collisions.

[COMPLETED] 13. Dependency Redundancy

Problem: Linking two nodes via multiple shared resources creates duplicate links in the graph, stressing atomic operations.

Solution: Add a check to ensure only one dependency link exists between any two specific nodes in a graph session.

[COMPLETED] 14. Unused Metadata

Problem: TaskMetadata is stored in every node but never used for scheduling decisions like priority or affinity.

Solution: Utilize metadata in the TaskScheduler to optimize execution order or hardware placement.

[COMPLETED] Task Ownership in Scheduler

Problem: The scheduler does not increment the reference count of submitted tasks. If a task's sushi_ptr goes out of scope while running, it will lead to Use-After-Free.

Solution: Call add_ref() when a task is submitted and release() when it is successfully completed/polled.

[COMPLETED] 16. Allocator Free Performance

Problem: deallocate performs an O(N) linear search through all memory pages to find the owner. This is very slow for large pools.

Solution: Use a more efficient lookup structure (like a range map or sorted page array) for pointer-to-page mapping.

[COMPLETED] 17. Memory Fragmentation & Alignment

Problem: The memory pool uses hardcoded 256-byte slots regardless of allocation size (high fragmentation) and ignores requested alignment.

Solution: Support multiple bucket sizes and check that the slot start meets the user's alignment requirements.

[COMPLETED] 18. Context Ownership & Dangling Refs

Problem: If RuntimeContext is destroyed before the TaskScheduler or TaskGraph, they are left with dangling references to the context.

Solution: Implement shared ownership or ensure the RuntimeContext manages the lifecycle of all its subsystems (Scheduler, Discovery, etc.).

[COMPLETED] 19. Device Type Awareness

Problem: probe_hardware treats all devices (CPU, GPU, FPGA) the same and forces in_order queues.

Solution: Optimize queue properties and selection based on the specific device type and capabilities.

[COMPLETED] 20. Multi-Vendor PCI Discovery

Problem: discover_sycl_devices uses Intel-specific PCI extensions. Discovery will fail on NVIDIA/AMD hardware.

Solution: Use more generic ways (like SYCL 2020 aspects or custom vendor plugins) to retrieve PCI info across different brands.

[COMPLETED] 21. CPU NUMA Accuracy

Problem: CPUs are hardcoded to NUMA node 0. In multi-socket systems, CPUs should also be mapped to their specific NUMA nodes.

Solution: Use hwloc to find the specific PU (Processing Unit) affinity or NUMA node for CPU-based SYCL devices.

[COMPLETED] 22. Docker Support

Problem: Setting up the environment (Intel oneAPI, SYCL, HWLOC) is complex and takes time.

Solution: Create a Dockerfile to provide a consistent, ready-to-use development environment.

[COMPLETED] TaskScheduler Concurrency Data Race

Problem: The TaskScheduler uses a Chase-Lev deque which is Single-Producer Multiple-Consumer (SPMC). However, the main thread pushes directly to worker_queues[0] in submit() while worker_0 is simultaneously executing dispatch_loop() and popping from the bottom.

Solution: Implement a thread-safe multi-producer MPMC injector queue for external submissions, and separate it from the workers' individual Chase-Lev deques.

[COMPLETED] 24. Lock-Free Pool Bypassing Race

Problem: In USMManagerImpl::allocate_new_page, when expansion_lock_ detects another thread is expanding the pool, it yields only once and immediately returns false, bypassing the high-performance pool.

Solution: Implement a proper spin-wait loop (while(expansion_lock_.test) { yield(); }) inside allocate_new_page so concurrent requests wait for expansion instead of bypassing it.

[COMPLETED] 25. Strategy Hardcoding / OCP Violation

Problem: The USMManagerImpl violates the Open/Closed Principle (OCP) by hardcoding allocation logic inside a monolithic if-else chain.

Solution: Refactor to use the Strategy Pattern by defining an IAllocatorStrategy interface.

[COMPLETED] 26. TaskGraph Execution Overreach / SRP Violation

Problem: TaskGraph::execute manages DAG compilation, thread creation, future/promises, and synchronization nodes, violating the Single Responsibility Principle (SRP).

Solution: Move DAG execution and thread orchestration into a centralized RuntimeContext or a dedicated ExecutionEngine.

[COMPLETED] Event Chaining Unimplemented Feature

Problem: The asynchronous scheduling currently misses direct SYCL event chaining from device to device queuing.

Solution: Fully implement the direct passing of SYCL event dependencies from Node::sycl_deps to the q.submit loop.

[COMPLETED] 28. Hardware Filtering Configuration

Problem: Hardware discovery forces the enumeration and mapping of all available SYCL devices.

Solution: Implement configuration properties to allow users to filter topologies.

[PENDING] 29. Critical: Thread Local Cache (TLC) "Use-After-Free" Crash

Problem: t_cache is defined as a global thread_local variable in memory_pool.hpp. When a thread terminates, the ~ThreadLocalCache() destructor attempts to access its owner MemoryPage to return memory slots. If the RuntimeContext (and thus the USMAllocator that owns these pages) has already been destroyed, this results in a guaranteed Segmentation Fault (Use-After-Free).

Solution: Tie the lifecycle of the Thread Local Cache to the USMAllocator. Alternatively, utilize a std::weak_ptr or a globally synchronized validity flag within the destructor to verify that the target MemoryPage still exists before accessing it.

[PENDING] 30. Critical: Event Polling Capacity Limit (Silent Deadlock)

Problem: The ActiveEventList in event_polling.hpp has a strictly hardcoded capacity of MAX_EVENTS = 256. If more events are pushed concurrently, it simply logs a warning and drops the event. The dropped event will never be marked as completed, causing its successor tasks to wait infinitely, resulting in a silent deadlock of the entire execution graph.

Solution: Remove the fixed 256 limit. Implement a dynamically resizing structure (such as a std::vector or a lock-free linked list of arrays). If a fixed capacity must be maintained for performance, the scheduler must block and wait() for older events to clear when capacity is reached, rather than dropping them.

[PENDING] 31. Critical: Global Lock Contention via rand() in Work Stealing

Problem: Inside scheduler.cpp, find_task_to_steal uses rand() % num_workers to pick a victim queue. The standard C rand() function relies on a hidden global mutex in many standard libraries (like glibc). When multiple idle workers call this concurrently, they form a massive bottleneck on this hidden lock, destroying the performance benefits of the lock-free Chase-Lev deque.

Solution: Replace rand() with a fast, thread-local, lock-free Pseudo-Random Number Generator (PRNG). For example, use a thread_local uint32_t state with a fast Xorshift algorithm, or thread_local std::mt19937.

[PENDING] 32. Critical: Read-Lock Bottleneck in Lock-Free Allocator

Problem: The memory deallocation process (USMManagerImpl::deallocate) uses a std::shared_mutex to acquire a read-lock when searching for a pointer's owner page (find_owner_page). At high thread counts, continuously taking a read-lock causes severe cache-line invalidation (ping-ponging) on the lock's atomic counter, heavily degrading the throughput of the supposedly lock-free memory pool.

Solution: Remove the shared_mutex from the deallocation path. Replace the sorted_pages_ vector with a fully lock-free lookup mechanism, such as a lock-free Radix Tree or a direct memory page-mapping table.

[PENDING] 33. Architectural: Liskov Substitution Principle (LSP) Violation

Problem: The base Task interface forces a specific type-tagging method (is_graph_node()). The EventPolling class uses this to downcast a Task* to a Node* via static_cast in order to trigger successors. This violates LSP, as the core scheduling system cannot treat all Task objects interchangeably without knowing their specific derived implementations.

Solution: Move the responsibility of triggering successors into the tasks themselves. Add a virtual on_complete() method to the base Task interface. Node can implement this to handle its dependency resolution, allowing EventPolling to remain completely ignorant of derived types.

[PENDING] 34. Architectural: Single Responsibility Principle (SRP) in TaskGraph

Problem: TaskGraph::compile() is currently responsible for analyzing DAG dependencies (finding roots) AND generating/injecting the internal "Sentinel" synchronization node. Injecting tracking nodes and handling execution promises should be the exclusive responsibility of the ExecutionEngine.

Solution: Move the creation and injection of the sentinel node out of TaskGraph and directly into ExecutionEngine::execute(). TaskGraph::compile() should only return the pure structural representation of the DAG (the nodes and the identified roots).

[PENDING] 35. Unlinked Feature: Stateless Memory Pool Design

Problem: The BitmaskMemoryPool is defined as a class, but it contains zero internal state (no member variables). It only serves as a wrapper to execute functions on MemoryPage structs passed in as arguments, making it an architectural anomaly rather than a true object.

Solution: Either convert BitmaskMemoryPool into a namespace containing static utility functions, or move the allocate_from_pool and deallocate_to_page logic directly into the MemoryPage struct as member methods to achieve proper encapsulation.

[PENDING] 36. Unlinked Feature: Incomplete Host Fallback Execution

Problem: In TaskGraph::add_task, if a task lacks device work, it is assigned ExecutionMode::HOST. However, the ExecutionEngine still strictly submits this "host" task into a SYCL hardware queue using q.submit(). If the system has no available SYCL queues, the fallback pathway will fail entirely.

Solution: Implement a pure native C++ thread execution path in the ExecutionEngine or TaskScheduler. If a task is marked as HOST, the worker thread should execute the payload directly on the CPU, bypassing the SYCL queue submission mechanism entirely.

[PENDING] 37. Unlinked Feature: Missing DAG Cancellation Mechanism

Problem: Once a large graph (e.g., 1 million nodes) is submitted to the engine, there is no mechanism to abort or cancel it. Calling scheduler.stop() drains the queue, but inflight SYCL kernels continue running, and the internal dependency counters (in_degree) of the remaining DAG nodes are not safely reset.

Solution: Implement a cancellation token system. Add an atomic is_cancelled flag to the RuntimeContext and pass it down. Worker threads and EventPolling should check this flag and gracefully discard tasks instead of executing them if a cancellation has been requested.