# link_security tests

The tests of the `link_security` module, mirroring `modules/extension/link_security/`.
`tests/extension/link_security/CMakeLists.txt` builds them into `sr_link_security_test_objects`, linked into `sr_extension_tests`. `sr test`
runs them with the rest; `sr run sr_extension_tests -- --gtest_filter='<pattern>'` runs one
program alone.

The tests need no hardware and no network. They pin the two primitives the protocol v4
handshake is built on, SHA-256 and HMAC-SHA256, to the vectors their standards publish, then the
proofs built from them.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_sha256.cpp

The expected digests are the literal values of the FIPS 180-4 example messages: the empty string,
"abc", the 448-bit and the 896-bit message, and one million times 'a'. None is computed by the
test. `PiecewiseUpdateMatchesOneShot` feeds the million bytes in pieces of 127, which never line up
with the 64-byte block, and then hashes a second message with the same object.

## test_hmac_sha256.cpp

| Test | What it proves |
| --- | --- |
| `HmacRfc4231Vectors` | Test cases 1, 2, 3, 4, 6 and 7 of RFC 4231. Case 5 truncates the code to 128 bits, which the handshake never does. Cases 6 and 7 use a 131-byte key, longer than a block, so they cover the path that hashes the key first. |
| `EqualInConstantTimeSeesEveryByte` | The comparison finds a change of one bit in any of the 32 bytes of a digest. |

## test_handshake.cpp

| Test | What it proves |
| --- | --- |
| `ProofsMatchAnIndependentImplementation` | The two expected proofs come from Python's `hmac` module for the same secret (bytes 0 to 31), the same nonces (32 times 0x11 for the worker, 32 times 0x22 for the master) and the labels `SRDP2 master` and `SRDP2 worker`. The test fails when the label, the order of the nonces or the key changes, which is a change to the wire protocol. |

### `ProofsDifferByRole`, `WrongSecretFails`, `FreshNonceDefeatsReplay`

Every nonce in these tests is a fixed value, so each run checks the same bytes. The first shows
that a proof sent back under the other role is refused, the second that a proof made under another
secret or under an empty key is refused, and the third that a recorded proof fails once either
side contributes a new nonce.

| Test | What it proves |
| --- | --- |
| `PlatformNonceSourceDoesNotRepeat` | The one test that reads `std::random_device`. It asks `make_nonce()` twice and expects two different values, neither of them all zeros. Two honest draws of 256 bits collide with probability 2^-256, so a failure means the source is broken. |
