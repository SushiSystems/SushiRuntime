# interop

The non-template entry points behind the `Interop` facade. The headers are in
`include/SushiRuntime/interop/`.

## interop.cpp

### export_host

`export_host` builds the `UsmAllocationOwner` before it makes the allocation that owner will
free. A throw between the two, for example `bad_alloc` from `make_unique`, then never leaves
an allocation that nothing records.
