# MW4 and Warzone: technical anti-cheat evidence

Reviewed **2026-10-31**. Start with **[technical/mw4-warzone.jsonl](technical/mw4-warzone.jsonl)** for AI ingestion: 58 cited core entries and 16 reproduced static observations, approximately 74 KB. The file carries source metadata, dates, build hashes, evidence labels and exact report pointers. Its 29 sources include one inspected diagnostic artifact.

The most concrete new code evidence comes from Activision's **Secure Attestation Wizard 1.3.0**. Its official support listing covers MW4, Warzone and BO7. This is a diagnostic utility; it does not expose the proprietary kernel driver or the game's complete attestation implementation.

## What can be established for these games

| Layer | Evidence and scope | Code detail available |
| --- | --- | --- |
| PC kernel component | Current Activision FAQ; historical update announcements | Purpose/lifecycle statements. Private routines, IOCTL layouts and detection rules remain undisclosed. C002–C004, C059, C062. |
| Local readiness/enrollment | Official diagnostic release inspected as data | Native exports/imports, managed checks, key/service literals, CLR method tokens, RVAs and selected IL references. T001–T016. |
| Remote attestation | Vendor statements plus Microsoft protocol/sample source | Public platform APIs and example exchange logic, separately labeled from Activision code. C083, C094, C230–C231, C237–C242. |
| Server Culling | October vendor interview | MW4 launch and Warzone Season 1 are the stated scopes, across PC and console. Exact replication/visibility algorithm is not published. C232–C233. |
| Server snapshot decoys | Activision's 2023 Hallucinations paper | Concrete historical serialization design; not proof that the same code runs in MW4. C186–C190. |
| DMA protections | October 2026 official announcement | Deployment statement; no verified algorithm or universal IOMMU requirement. C183. |

Keep access rules scoped: the support page distinguishes MW4's all-online-mode requirements from Warzone's restricted modes/matchmaking treatment. C110–C115 and C229 preserve the wording and date.

## Official diagnostic artifact

The ZIP was obtained from the download link on [Activision Support](https://support.activision.com/articles/trusted-platform-module-and-secure-boot#AttestationWizard). Binaries are kept outside Git. The repository publishes original metadata summaries and reproducible analysis code.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| Official ZIP | 61,580,746 | `75a49a5743cc975fed640a65e333419f45caa544bc93a2d0d55972d660d0b1b2` |
| Outer wizard executable | 67,343,400 | `5bfbedb08b94cefae839216c8df04384a679e157f280b81c889acc89325d4d06` |
| Bundled SecureAttestation.dll | 212,480 | `628fbea8e9648006893c830c9e52b0f8a39eb73919ba8d42654da909c1a294f5` |
| Bundled enrollaik.exe | 90,184 | `01e3b8bd3e4c22f481fba82f1ee225389ea2aa08dbcefde9672142a38e50a4ce` |
| Managed wizard DLL | 450,560 | `46422603e2231cdb96bc0f167b5de7e234282a91583f4810f33a948d5eb01168` |

[provenance.json](technical/provenance.json) records the official URL, release/date distinction, storage-object metadata and format references. [wizard-1.3.0-static.json](technical/wizard-1.3.0-static.json) contains the full generated metadata report. Certificate subjects were parsed; Authenticode signature validity and trust were not verified.

The single-file .NET bundle uses manifest format 6.0 with 382 entries. Embedded `version.json` identifies 1.3.0 and October 21, while the outer PE version resource remains 1.0.0.0. Runtime configuration declares .NET 9.0.5. A version resource or PE timestamp should not be substituted for the release metadata.

## Actual interfaces and managed call paths

| Component / method | Static location | Observed content |
| --- | --- | --- |
| Native readiness export | SecureAttestation.dll RVA `0x7a60` | `SecureAttestationReadinessVerify` |
| Native diagnostic export | SecureAttestation.dll RVA `0x8d40` | `SecureAttestationGetDiagnosticInfo` |
| `calculateData` | Managed token `0x060000fc`, RVA `0x5b7b8` | Allocates 13 CheckInfo entries; calls readiness P/Invoke at IL `0x11`. |
| `get_IsPassed` | Token `0x06000061`, RVA `0x59230` | Loads Value and DesiredValue, then compares equality. |
| `GetDiagnosticInfo` | Token `0x06000042`, RVA `0x58a78` | Size query, buffer fill, bounds checks and UTF-8 decoding. |
| `GenerateAikKey` async state machine | MoveNext token `0x0600013d`, RVA `0x5c384` | Locates bundled enrollaik.exe and calls the elevation helper. |
| `StoreAikEnrollmentResult` | Token `0x06000083`, RVA `0x5994c` | Contains a local registry result-storage path and SetValue call. |

Locations refer to **decompressed components**, not the ZIP or outer executable. The report distinguishes file offsets, RVAs, method tokens and IL offsets, and hashes each selected method's instruction bytes. A call reference establishes a code path; this investigation did not execute it.

The native DLL imports these useful API families:

- TPM Base Services: device information, contexts, command submission, and TCG-log retrieval.
- CNG key storage: opening a provider/key and reading properties.
- Windows firmware-variable access, registry queries, service configuration/control and DeviceIoControl.

Those imports do not identify a private RICOCHET IOCTL, prove that every path runs, or establish a detection schedule. The DLL also contains `ActivisionAIK`, TPM firmware/manufacturer property names and `COD.Broker.Service`. The enrollment helper contains a named-key enrollment command prefix. Neither observation establishes how hardware bans are keyed.

## Readiness data model

The managed enum contains the following identifiers. These are diagnostic checks, not 13 kernel cheat detections.

| Value | Check |
| ---: | --- |
| 0 | TPM2_PRESENT |
| 1 | AMDFW_NEEDS_UPDATE |
| 2 | SHOULD_TURN_ON_SECURE_BOOT |
| 3 | CAN_TURN_ON_SECURE_BOOT |
| 4 | TCG_LOG_PRESENT |
| 5 | EFI_MEASUREMENT_PRESENT |
| 6 | GPT_BOOT_PARTITION |
| 7 | UEFI_BIOS_MODE |
| 8 | AIK_KEY_EXISTS |
| 9 | BAD_KEYSET |
| 10 | AIK_CERT_PRESENT |
| 11 | COD_BROKER_INSTALLED |
| 12 | COD_BROKER_DISABLED |

`COUNT=13` is the sentinel. CheckInfo has `check`, `checkCategory`, `value`, `desiredValue` and `level` fields. Categories are TPM, ATTESTATION and SECURE_BOOT. Levels are REQUIRED, EXPECTED and INFORMATIONAL. The inspected getter implements the following expression; this is an original transcription of its IL, not released Activision source:

```python
is_passed = (value == desired_value)
```

Consequently, a consumer must retain the desired value rather than equating success with every flag being true. T004–T005 provide the exact metadata and code references. The analysis does not infer the game's server admission decision from a wizard result.

## Public code worth reading

Microsoft's [Attestation-Client-Samples](https://github.com/microsoft/Attestation-Client-Samples/tree/84a502dcbedb674b505262f3eee022fd88ebc3ef) is pinned to commit `84a502dcbedb674b505262f3eee022fd88ebc3ef`.

| Source | What to inspect | Boundary |
| --- | --- | --- |
| [attest.cpp](https://github.com/microsoft/Attestation-Client-Samples/blob/84a502dcbedb674b505262f3eee022fd88ebc3ef/attestation/attest.cpp) | Session/exchange/report loop and AttestTpm call | Example transport/authentication, not COD's implementation. |
| [sample_boot_att.cpp](https://github.com/microsoft/Attestation-Client-Samples/blob/84a502dcbedb674b505262f3eee022fd88ebc3ef/attestation/sample_boot_att.cpp) | AIK, request-key and relying-party input construction | The fixed nonce is explicitly a placeholder. |
| [EnrollAik.ps1](https://github.com/microsoft/Attestation-Client-Samples/blob/84a502dcbedb674b505262f3eee022fd88ebc3ef/EnrollAik.ps1) | Named AIK enrollment and optional key-file permission handling | Reviewed as text; no key or ACL changes performed. |

The [Microsoft TPM protocol](https://learn.microsoft.com/en-us/azure/attestation/virtualization-based-security-protocol) gives message fields and Windows API references. Basic TPM attestation and VBS evidence are separate protocol cases. C230–C231 and C237–C242 contain the concise code summaries. The wizard's readiness imports do not prove which full attestation protocol/API path the game uses.

For server code, the [2023 Hallucinations paper](https://www.activision.com/cdn/research/hallucinations) is the strongest published implementation description: manipulate a copied player entry during snapshot serialization after authoritative gameplay updates, before client processing. Preserve its historical scope. The new Server Culling announcement explains withholding information, but publishes no corresponding source, visibility thresholds or latency measurements.

## Reproduce the static findings

The fetcher downloads only the reviewed public ZIP and checks its hash. The inspector parses it as data in memory. It does not launch the wizard, enrollment helper, game or driver.

```sh
python -m venv .venv
.venv/bin/python -m pip install -r tools/requirements-static.txt
python tools/fetch_wizard.py
.venv/bin/python tools/inspect_wizard.py \
  .evidence-cache/CODSecureAttestationWizard_1.3.0.zip \
  --expected-sha256 75a49a5743cc975fed640a65e333419f45caa544bc93a2d0d55972d660d0b1b2 \
  --out .evidence-cache/reproduced-static.json
python tools/test_static_inspector.py
python tools/validate_technical.py
```

The report records parser package versions. With the reviewed dependency versions, compare its SHA-256 with [provenance.json](technical/provenance.json). Replacing the published report with another build requires a new review; a matching filename is insufficient.

Fast, citation-complete retrieval uses the existing standard-library query tool:

```sh
python tools/query.py --file technical/mw4-warzone.jsonl --id T004 --id T005
python tools/query.py --file technical/mw4-warzone.jsonl --status static_observation
python tools/query.py --file technical/mw4-warzone.jsonl --topic attestation_code
```

Rebuild focused summaries after a reviewed core/report change with `python tools/build_technical.py`; then validate them. See [technical/OPEN_QUESTIONS.md](technical/OPEN_QUESTIONS.md) for the precise implementation questions still unresolved.
