# laith-km-driver: complete original archive and source snapshot

The **[complete original RAR](https://raw.githubusercontent.com/ll2Rawll/ricochet-research/main/artifacts/laith-km-driver/original/laith-km-driver-main.rar)** is preserved in this repository, including all 189 files and 49 directories. The [`source/`](source/) directory provides a browsable 48-file source/documentation snapshot. The original archive is byte-for-byte identical to the [user-provided GoFile download](https://gofile.io/d/G6JoV856).

[Download the full RAR](https://raw.githubusercontent.com/ll2Rawll/ricochet-research/main/artifacts/laith-km-driver/original/laith-km-driver-main.rar) · [Full member inventory](archive-inventory.json) · [Preservation metadata](archive-preservation.json) · [Browse source](source/)

| Item | Value |
| --- | --- |
| Original archive | `original/laith-km-driver-main.rar` |
| Archive size | 92,462,917 bytes |
| Complete contents | 189 files and 49 directories; 835,463,111 expanded file bytes |
| Browsable source snapshot | 48 original files; 475,566 bytes |
| Additional files inside the RAR | 141 files; 834,987,545 bytes |
| Archive SHA-256 | `dc533581014f6c8d58c86b6f38ce1ab7e583a01ccee7983b1f947dc64ef3f2d5` |

All Visual Studio `.vs/` caches, build outputs, per-user project settings and binary samples are included in the complete RAR. They are omitted only from the browsable `source/` tree. The [original import manifest](manifest.json) records that earlier extraction choice and remains unchanged to preserve pinned evidence hashes. The new [full inventory](archive-inventory.json) records a SHA-256 hash for every regular archive member.

Source bytes and the original [MIT license](source/LICENSE) are retained. Within the browsable snapshot, the supplied `.gitattributes` is preserved as `.gitattributes.upstream` and repository attributes preserve exact bytes across checkout settings. The RAR retains every original name and byte. The local [ignore rules](.gitignore) prevent generated output from being accidentally added as loose files; they do not remove content from the preserved archive.

## Reported working history

On 1 November 2026, the user who supplied the archive reported that the project previously worked perfectly. The report did not specify the test date, Windows/game build, hardware or settings. This repository review has not independently reproduced that runtime result. Earlier “not tested” labels describe the checks performed during this import/review, not a finding that the project never worked.

The project-file platform declarations, external include-path entries and license wording remain documented as observed. Those metadata facts do not disprove the user's prior result.

## Research and original files

- [Evidence and Windows platform review](../../research/laith-km-driver-review/README.md)
- [Structured research data](../../research/laith-km-driver-review/review.json)
- [Original project README](source/README.md)
- [Visual Studio solution](source/Driver.sln)
- [Kernel project](source/km/) and [hypervisor project](source/hv/)
- [Original architecture notes](source/GEMINI1.md)

Documentation metadata is catalogued in the research library. Supplied feature descriptions and prior-use reports remain attributed to their authors; they are not independently reproduced descriptions of a current RICOCHET implementation.

## Verify the complete archive

From `artifacts/laith-km-driver/original/`:

```sh
sha256sum -c SHA256SUMS
```

All 189 regular members were decompressed without execution and their sizes/hashes recorded. No decompression/checksum errors were reported. No archive script, build step, binary or driver was executed during this review.

To verify the browsable source files, run `sha256sum -c SHA256SUMS` from `artifacts/laith-km-driver/`. From the repository root, `python scripts/validate_research.py` checks the research, the complete archive, its inventory and the preserved source snapshot.
