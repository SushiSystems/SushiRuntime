# api/interop

The Buffer-typed side of memory pairing with another API: the Interop facade and HostExport.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

The vocabulary the facade speaks (ExternalMemoryDescriptor, InteropCapabilities, DeviceUuid) and the
exported functions it forwards to are in `include/SushiRuntime/interop/`, which knows nothing about
Buffer. This folder depends on that one, never the reverse.

## interop.hpp

### Interop

Runtime::interop() returns the facade by value and is the only way to get one: the constructor is
private and Runtime is its friend, so a facade never holds a null context. It holds a shared_ptr to
the RuntimeContext, and so
does every Buffer it returns, so the context stays alive until the last of them is destroyed.

capabilities() is the question to ask before either call below. host_export is true when the device
offers host USM (sycl::aspect::usm_host_allocations). device_import is true only when the toolchain has the bindless-images extension, the
device is a GPU, it reports the external-memory-import aspect and it reports a UUID. device_uuid is
empty when the device does not report one.

### export_host

export_host allocates host USM on the given device's queue and returns it as a Shared Buffer. The
alignment is raised to at least alignof(std::max_align_t) and must be a power of two; the byte size
is rounded up to a multiple of it, and HostExport::bytes holds the rounded size. A count of zero
returns an empty Buffer, a null address and zero bytes without allocating. A bad alignment, a size
that overflows when rounded, or a failed allocation throws SushiException with allocation_failed.

### import_device

import_device maps memory another API exported and returns it as a Device Buffer: a kernel may use
it, the host may not index it. The runtime records the mapped address in its USM registry, so a task
that touches the Buffer is routed to the importing device like any other.

The call throws SushiException with unsupported when the device cannot import (see capabilities()),
and with invalid_access when the descriptor's kind is Unspecified (the default) or belongs to
another platform, when an OpaqueWin32 handle is null or an OpaqueFd handle is above INT_MAX, when
its region ends past allocation_bytes, or when the device's UUID differs from
ExternalMemoryDescriptor::device. A throw part-way through releases whatever steps had completed.

A count of zero with a descriptor that passes those checks returns an empty Buffer and imports
nothing.

SushiRuntime never closes or duplicates ExternalMemoryDescriptor::handle; the caller keeps
ownership of it. `modules/surface/interop/README.md` lists each point of the call and what is not
established about the SYCL backend.

Destroying the Buffer removes the address from the registry, unmaps the region and releases the
SYCL import handle, in that order. A step that fails is logged at ERROR and the later steps still
run. It makes no other call, so freeing the allocation stays with the API that exported it.

## host_export.hpp

### HostExport

buffer owns the allocation. address and bytes are copies taken at export time, for handing to the
importing API; address equals buffer.data(). Moving buffer out of the struct keeps address valid,
because the allocation moves with it. address dangles once the Buffer that owns the allocation is
destroyed, so that Buffer must outlive every import the other API made from it.

### Interop::HostExport

SushiEngine spells the type Interop::HostExport, and so does the return type of export_host. The
alias makes that spelling name API::HostExport, so there is one type under two names.
