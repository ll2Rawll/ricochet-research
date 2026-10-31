# Validation record

Checked on 2026-10-31 in Linux with Python 3.12.14 and GCC 14.2.0.

| Check | Result |
| --- | --- |
| Python behavioral suite | 73 tests passed |
| Python package | Wheel built successfully with no runtime dependencies |
| Portable end-to-end demo | Passed: tamper/replay rejection, three expected fictional rule findings, integrity change, visibility selection and journal checkpoint |
| Loopback client/server | Passed through real local sockets and CLI subprocess |
| Portable C ABI check | Passed: protocol 1, event 56 bytes, batch header 40 bytes, full batch 1832 bytes |
| WDK project XML | Parsed successfully; this is not an MSBuild compilation |
| Windows driver and native collector build | Not performed: Windows SDK/WDK and MSVC are unavailable in this environment |
| Windows kernel runtime / Driver Verifier | Not performed |
| RICOCHET/MW4 compatibility or detection coverage | Not measured; no such compatibility is implemented |

The suite includes adversarial input cases: duplicate keys and lengths, non-finite numbers, altered HMACs, reused nonces, wrong sessions and sequences, concurrent acceptance, expired sessions, full session capacity, symlink/path escape attempts, truncated and edited journals, malformed collector batches, browser origins, unexpected hosts, redirects and oversized client responses.

Review revisions corrected the loopback client's redirect/response boundary and checked the driver's requestor-process API header. Results describe this exact source and its test cases; they are not evidence of equivalence to a private anti-cheat product. Follow [the Windows validation plan](../windows/README.md#validation-still-needed-on-windows) before making any native runtime claim.
