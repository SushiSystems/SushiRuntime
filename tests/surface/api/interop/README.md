# api tests: interop

The tests of importing and exporting memory. They are part of `sr_api_tests`;
[the page of the module's tests](../README.md) says how to run them.

## test_interop.cpp

The file covers what `import_device` refuses and what `capabilities()` reports. It builds a
`Runtime` over the devices of the machine it runs on. No test imports a real allocation. The descriptors hold a placeholder
handle of 1; the refusal tests expect a refusal, and the zero-element test requests no elements.

### `first_device_where_import_is`

Three tests need a device that imports external memory, which means a GPU on a toolchain with the
bindless-images extension. One needs a device that does not. The helper finds either, and a test
whose device is missing is reported as skipped.

### `acceptable_descriptor`

`acceptable_descriptor` builds a descriptor with this platform's handle kind, the placeholder handle 1,
an allocation of 64 bytes and the UUID of the chosen device. Each test then breaks one field, so the refusal it sees
can only come from that field.

### `HostExportReportsWhetherTheQueueOffersHostUSM`

`host_export` equals `sycl::aspect::usm_host_allocations` of the device behind the queue.

### `ADeviceWithoutImportIsRefusedByItsIndex`

The capability is checked before the descriptor. The refusal is `ErrorCode::Unsupported` and its
message carries the device index.

A toolchain without bindless images refuses earlier, before it reads any device, with a message that
names the toolchain. The test skips there.

### `AHandleKindOfAnotherPlatformIsRefusedByName`

Windows imports `OpaqueWin32` handles and every other platform imports `OpaqueFd`. The other kind is
refused with `ErrorCode::InvalidAccess`.

### `ARegionPastTheAllocationIsRefusedWithItsNumbers`

Four floats at offset 60 end at byte 76 of a 64-byte allocation. The message carries the byte
count, the offset and the allocation size.

The second case puts the offset past the allocation and asks for zero bytes. The check compares the
offset first, so the subtraction that follows cannot wrap.

### `AnotherDevicesUUIDIsRefusedWithBothUUIDs`

The descriptor carries the device's own UUID with the last byte inverted. The message spells both
UUIDs in hexadecimal. A device that reports no UUID is refused with its own message; no test
reaches that case, because a device cannot be made to drop its UUID.

### `ADeviceThatImportsReportsItsUUID`

For every device that imports external memory, the capabilities report a device UUID.

### `ADefaultDescriptorIsRefusedForItsUnstatedKind`

A default-constructed descriptor is refused with `ErrorCode::InvalidAccess` on the first device that
imports. The message says that `descriptor.kind` is Unspecified and that the exporter must state it.

### `ANullWin32HandleIsRefusedByName`

Compiled on Windows only. A descriptor whose handle is 0 is refused with `ErrorCode::InvalidAccess` on the
first device that imports, and the message says that `descriptor.handle` is null.

### `AFileDescriptorAboveIntIsRefusedWithItsValue`

Compiled on platforms other than Windows only. A handle one above the largest `int` is refused with
`ErrorCode::InvalidAccess` on the first device that imports, and the message spells the handle value.

### `ZeroElementsImportNothingAndReturnAnEmptyBuffer`

`import_device` with a count of zero, on the first device that imports, returns a buffer that is empty
and has size zero.

## test_host_export.cpp

The file tests exporting host memory a device and the host both reach.

