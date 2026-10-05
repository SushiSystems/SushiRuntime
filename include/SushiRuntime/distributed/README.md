# distributed

The Master and worker layer that ships named kernels and buffer references across machines. It is
compiled only under SR_DISTRIBUTED.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

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
