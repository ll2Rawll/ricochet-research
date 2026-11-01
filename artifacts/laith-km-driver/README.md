# laith-km-driver source archive

Source-only import of the user-provided [GoFile archive](https://gofile.io/d/G6JoV856), reviewed 1 November 2026. The original project is preserved under [`source/`](source/) with original file bytes unchanged. The original `.gitattributes` is retained verbatim as `.gitattributes.upstream`; a replacement Git metadata rule prevents checkout-time line-ending changes.

| Item | Value |
| --- | --- |
| Original archive | `laith-km-driver-main.rar` |
| Download size | 92,462,917 bytes |
| Expanded files | 189 files; 835,463,111 bytes |
| Imported files | 48 files; 475,566 bytes |
| Omitted files | 141 files; 834,987,545 bytes |
| Archive SHA-256 | `dc533581014f6c8d58c86b6f38ce1ab7e583a01ccee7983b1f947dc64ef3f2d5` |

The import keeps the Visual Studio solution, both projects, C++/header/assembly sources, INF, original documentation and helper scripts, and [MIT license](source/LICENSE) (Copyright 2025 LAITHCOOL). It preserves the separate license wording in the supplied README without reconciling it.

Visual Studio `.vs/` caches, build/x64 output trees, per-user `.vcxproj.user` settings, and two unreferenced `hidclass.sys` binary samples were omitted. Neither the compressed RAR nor compiled drivers are checked into this source snapshot. [The manifest](manifest.json) records every archive entry, included-file hashes, and each omission reason. The local [ignore rules](.gitignore) keep regenerated output out of future commits. Two repository-added `.gitattributes` files preserve exact bytes on Windows and Unix; they are separate from the 48 retained original files.

## Research review

[Read the evidence and Windows platform review](../../research/laith-km-driver-review/README.md) for project metadata, primary references and a record of what remains untested. [Structured review](../../research/laith-km-driver-review/review.json) links the added findings to the research corpus.

## Browse the supplied files

- [Original project README](source/README.md)
- [Solution](source/Driver.sln)
- [Kernel project](source/km/)
- [Hypervisor project](source/hv/)
- [Original architecture notes](source/GEMINI1.md)

The original documentation contains author/AI-generated claims and machine-specific links. Those claims are retained as supplied and are not research findings or verified descriptions of RICOCHET. This is a user-provided kernel-driver/hypervisor project; the import does not establish its authorship, runtime behavior, safety or current-game applicability.

## Verify the imported snapshot

From this directory, run:

```sh
sha256sum -c SHA256SUMS
```

All 48 imported files were decompressed and hashed; explicit project source/header/assembly/INF paths were checked for presence. Excluded members were inventoried without a full decompression/CRC check. No archive scripts, build steps, binaries or drivers were executed.

The original Windows/WDK projects retain machine-specific include paths and differing platform configurations. Build portability and functionality have not been tested. Import optimization concerns storage and repository organization; source behavior and project settings are unchanged.
