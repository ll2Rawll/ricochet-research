# Imported driver project: evidence and platform context

Research revision 14 · reviewed 2026-11-01 · source A05

This review is tied to [the imported snapshot at `75570c7`](https://github.com/ll2Rawll/ricochet-research/tree/75570c746672865d48420b724efbe8c55ab6ae51/artifacts/laith-km-driver/). It examines archive identity, licensing and project metadata. It does not authenticate an Activision/RICOCHET implementation or establish MW4/Warzone compatibility.

[Machine-readable review](review.json) · [Original-file checksums](../../artifacts/laith-km-driver/SHA256SUMS) · [Import manifest](../../artifacts/laith-km-driver/manifest.json) · [Research library](../../README.md)

## What the artifact establishes

| ID | Reproduced observation | Evidence |
| --- | --- | --- |
| F021 | The import manifest identifies laith-km-driver-main.rar as 92,462,917 bytes with SHA-256 dc533581014f6c8d58c86b6f38ce1ab7e583a01ccee7983b1f947dc64ef3f2d5; file identity does not authenticate its producer. | [manifest.json](../../artifacts/laith-km-driver/manifest.json) |
| F022 | The manifest retains 48 original files totaling 475,566 bytes and omits 141 files totaling 834,987,545 bytes, including IDE caches, build outputs, local settings and unreferenced binary samples. | [manifest.json](../../artifacts/laith-km-driver/manifest.json) |
| F023 | Driver.sln registers km and hv projects. Their XML declares WindowsKernelModeDriver10.0 with KMDF and WDM driver types respectively; these are configuration declarations, not verified runtime architecture. | [Driver.sln](../../artifacts/laith-km-driver/source/Driver.sln); [hv.vcxproj](../../artifacts/laith-km-driver/source/hv/hv.vcxproj); [km.vcxproj](../../artifacts/laith-km-driver/source/km/km.vcxproj) |
| F024 | All 31 explicit source, header, assembler and INF file references in the two project files resolve within the imported snapshot. This checks path availability, not compilation or link success. | [hv.vcxproj](../../artifacts/laith-km-driver/source/hv/hv.vcxproj); [km.vcxproj](../../artifacts/laith-km-driver/source/km/km.vcxproj) |
| F025 | The km project declares x64 and ARM64 platforms, while hv declares x64 only. Both contain Debug/Release configurations; solution-level platform labels do not prove support for each component. | [hv.vcxproj](../../artifacts/laith-km-driver/source/hv/hv.vcxproj); [km.vcxproj](../../artifacts/laith-km-driver/source/km/km.vcxproj) |
| F026 | The km project contains two AdditionalIncludeDirectories entries with an absolute external Windows path. Availability of those external inputs and build portability were not established. | [km.vcxproj](../../artifacts/laith-km-driver/source/km/km.vcxproj) |
| F027 | The separate LICENSE is MIT with a 2025 LAITHCOOL copyright, while the original README reproduces a generic 2024 MIT notice. Both texts are preserved; the discrepancy does not resolve authorship. | [LICENSE](../../artifacts/laith-km-driver/source/LICENSE); [README.md](../../artifacts/laith-km-driver/source/README.md) |
| F028 | The original archive .gitattributes is retained verbatim as source/.gitattributes.upstream; repository-added attributes preserve exact imported bytes across checkout line-ending settings. | [manifest.json](../../artifacts/laith-km-driver/manifest.json); [.gitattributes.upstream](../../artifacts/laith-km-driver/source/.gitattributes.upstream); [.gitattributes](../../artifacts/laith-km-driver/source/.gitattributes); [.gitattributes](../../artifacts/laith-km-driver/.gitattributes) |

The manifest describes the archive inventory, while the checksum list covers the 48 retained original files. Added repository metadata and this research review are separate. Excluded members were not all decompressed, so this review makes no complete-archive CRC claim.

## Reading the build metadata

| Project | Declared driver type | Declared platforms | Configurations | Explicit file references |
| --- | --- | --- | --- | ---: |
| `hv` | WDM | x64 | Debug, Release | 27 |
| `km` | KMDF | ARM64, x64 | Debug, Release | 4 |

Both project files name the Windows kernel-driver toolset. An XML value records an intended build setting; it does not prove which driver framework is used correctly or that the resulting binary can load. All listed file paths exist, but external includes, compiler/WDK versions, libraries and platform coverage have not been validated by a build.

## Windows reference context

This pass adds six platform findings and reuses C254/C259/C260 from the existing library rather than duplicating them. These are general platform requirements and research interpretation aids. They are not an implementation audit or instructions for changing this project.

| ID | Documented distinction | Primary reference |
| --- | --- | --- | --- |
| C452 | Microsoft recommends first considering whether a lower-risk application or Windows service can meet the requirement before introducing a kernel driver. | [S157: Driver security checklist](https://learn.microsoft.com/en-us/windows-hardware/drivers/driversecurity/driver-security-checklist) |
| C259 | Microsoft requires dispatch validation of the entire 32-bit IOCTL and input/output lengths, and advises against generic kernel-memory read/write control codes. | [S73: Security issues for I/O control codes](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/security-issues-for-i-o-control-codes) |
| C254 | METHOD_BUFFERED uses SystemBuffer for both input and output, with separate InputBufferLength and OutputBufferLength limits. | [S72: Buffer descriptions for I/O control codes](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/buffer-descriptions-for-i-o-control-codes) |
| C260 | IoValidateDeviceIoControlAccess supports stricter dynamic access checks; zeroing driver-allocated output buffers prevents accidental kernel-data disclosure. | [S73: Security issues for I/O control codes](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/security-issues-for-i-o-control-codes) |
| C453 | Microsoft memory-integrity guidance calls for non-executable allocations, separation of writable/executable sections and no direct modification of executable system memory. | [S158: Implement memory integrity compatible code](https://learn.microsoft.com/en-us/windows-hardware/drivers/driversecurity/implement-hvci-compatible-code) |
| C454 | Microsoft documents dedicated HLK code-integrity tests and Driver Verifier code-integrity checks for evaluating memory-integrity compatibility. | [S158: Implement memory integrity compatible code](https://learn.microsoft.com/en-us/windows-hardware/drivers/driversecurity/implement-hvci-compatible-code) |
| C455 | Microsoft describes Driver Verifier as a runtime driver-checking tool, warns it can crash Windows, and restricts its use to testing/debugging computers. | [S159: Driver Verifier](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/driver-verifier) |
| C456 | Windows driver security is based on securable objects, access tokens and security descriptors; drivers are responsible for protecting their devices and device objects. | [S160: Windows security model for driver developers](https://learn.microsoft.com/en-us/windows-hardware/drivers/driversecurity/windows-security-model) |
| C457 | Microsoft distinguishes WHCP certification combinations from general CodeQL use and publishes version requirements for the CLI, driver query packs and related C++ queries. | [S161: Run CodeQL Analysis on Windows Driver Code](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/static-tools-and-codeql) |

## Relationship to MW4 and Warzone

[Activision’s FAQ](https://support.activision.com/articles/ricochet-overview) describes a PC driver as one part of a broader system that also uses server tools, investigations and mitigations. Its [TPM/Secure Boot support page](https://support.activision.com/articles/trusted-platform-module-and-secure-boot) lists MW4 and Warzone requirements. Both pages were rechecked on this review date. These vendor statements do not endorse this uploaded project.

A signed file, a permitted Windows driver load, a valid device-access policy, memory-integrity compatibility and game-publisher support answer different questions. A positive result for one does not establish the others. Earlier library records C433–C447 provide signature and OS-policy context; this pass supplies no new game-runtime result.

## Evidence status and research gaps

| Question | Current evidence status |
| --- | --- |
| Are the retained files identifiable? | Yes: pinned snapshot, byte counts and SHA-256 values. |
| Do the project files list available local inputs? | Yes: 31 explicit file references resolve. External inputs remain unverified. |
| Is original publisher identity authenticated? | No. The license attribution is retained, not independently authenticated. |
| Does the package compile on an identified Windows/WDK combination? | Not tested. |
| Has CodeQL produced a reproducible analysis report? | No run performed. |
| Has Driver Verifier or HLK produced a passing result? | No run performed. |
| Is HVCI/Memory Integrity compatibility established? | No. |
| Does it work with a protected MW4/Warzone build? | Not tested or established. |

Useful future research evidence would identify the exact source revision, OS build, architecture, WDK/compiler versions, analysis-tool/query versions, and resulting logs. Missing results remain unknown rather than inferred success. Microsoft’s runtime testing reference limits Driver Verifier to testing/debugging computers because it can crash Windows. No driver or game experiment occurred in this review.

## Reproduce the data checks

From the repository root:

```sh
python scripts/validate_research.py
```

From `artifacts/laith-km-driver/`:

```sh
sha256sum -c SHA256SUMS
```

These verify research consistency, imported-file identity and project-path availability. They do not compile the driver or evaluate its runtime behavior.
