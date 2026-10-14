# link_security

The module proves that both ends of a new link hold the cluster's secret: SHA-256, HMAC-SHA256 with
a constant-time comparison, and the handshake proofs over the two nonces. It is an internal module:
its headers reach the modules that name it and the tests, and are never installed. It is built with
the distributed layer, under `SR_ENABLE_DISTRIBUTED`, and every name it declares is in
`SushiRuntime::Distributed`.

It depends on `core`.

## Public entry points

| Header | Holds |
| --- | --- |
| `sha256.hpp` | `Sha256`, `Sha256Digest`, `sha256()` |
| `hmac_sha256.hpp` | `hmac_sha256()`, `equal_in_constant_time()` |
| `handshake.hpp` | `HandshakeNonce`, `HandshakeRole`, `compute_proof()`, `verify_proof()`, `make_nonce()` |
