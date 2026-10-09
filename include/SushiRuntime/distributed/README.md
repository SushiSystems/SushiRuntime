# distributed

The Master and worker layer that ships named kernels and buffer references across machines. It is
compiled only under SR_DISTRIBUTED.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## cluster.hpp

### Cluster

Cluster::attach() is the way into the layer. It builds a master on a runtime, installs it as the
runtime's Graph::OffloadCoordinator and as the allocator's release observer, starts it, and returns
a handle to its knobs and counters. Until attach() runs, the runtime is a single machine: no header
of `api` or `execution` names a transport, and Buffer::distributed() does nothing.

One overload takes an API::Runtime and the other an Execution::RuntimeContext. The first forwards
to the second through Runtime::unsafe_context(); the second serves code that drives a bare context.

The handle is a view. Copies name the same master, and destroying one changes nothing, so a caller
may drop the handle straight after attach(). The runtime shares ownership of the master, stops it
when the runtime is destroyed and releases it after its engine has shut down. A handle that
outlives its runtime still reads the stopped master's counters.

### detach

detach() stops the master, clears the allocator's release observer and removes the master from the
scheduler, in that order. Every graph compiled afterwards runs locally. The caller must make sure
no run is in flight, because the scheduler reads the coordinator's address without a lock; the code
cannot check that. detach() does nothing when the runtime has already released the master, which
covers a destroyed runtime and a second attach() on the same runtime.

### bump_buffer and buffer_handle

Both read the master's buffer registry by USM address. Offload bumps versions itself at dispatch and
at completion; bump_buffer() is for a write the runtime did not see, and for tests that need a
stale version. A buffer enters the registry through Graph::OffloadCoordinator::share(), which
Buffer::distributed() and State::distributed() call.

## protocol.hpp

### Distributed wire protocol

protocol.hpp defines the distributed wire protocol.

The whole distributed layer exists behind SR_DISTRIBUTED (CMake option SR_ENABLE_DISTRIBUTED,
default OFF). When the option is off protocol.hpp is never compiled into the library, so the offline
single-machine build carries no distributed code, no extra dependencies and no ABI surface.

The core idea is that compiled code cannot travel the wire. A task is a C++ closure over local
pointers, and neither the closure nor the pointers mean anything on another machine. The protocol
therefore never serializes code or pointers. It serializes references to code that is already
compiled into every node (OpID) and references to data that each worker can resolve to its own local
USM (GlobalBufferID plus a version).

### ByteWriter and ByteReader

ByteWriter and ByteReader are a tiny length-prefixed serializer. Both ends share the same binary
and, in the target lab, the same architecture and endianness, so the format stays raw and trivially
copyable and no schema library is pulled in.

### ByteReader: bounds check

ByteReader checks bounds without computing pos_ + bytes. `bytes` is an attacker-controlled 64-bit
wire value, so pos_ + bytes can overflow and wrap to a small value that passes the check, yielding a
subspan about 2^64 long and an out-of-bounds read or a bad_alloc downstream. The subtraction is safe
because the invariant pos_ \<= size() always holds: the cursor only advances within bounds.

### TaskDispatchMsg: the name field

TaskDispatchMsg serializes meta's POD fields but not its `name`, a `const char*`. A pointer into the
master's address space is a wild pointer on the worker and an ASLR information leak on the wire. The
worker never dereferences a dispatched task's name, so serialization drops it and deserialization
restores a generic default.
