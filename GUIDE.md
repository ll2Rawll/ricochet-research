# Reading map

For the new MW4/Warzone code focus, start with [MW4_WARZONE_TECHNICAL.md](MW4_WARZONE_TECHNICAL.md) and [technical/mw4-warzone.jsonl](technical/mw4-warzone.jsonl). T001–T016 describe the hash-identified public diagnostic utility; C IDs retain the core evidence labels and dates.

Use these IDs in [ricochet.jsonl](ricochet.jsonl). Every retained entry contains a primary-text locator and scope. This is a navigation aid, not a reverse-engineered protocol.

| Area | Main-file entries | Evidence boundary |
| --- | --- | --- |
| Driver purpose and lifecycle | C001–C007, C059, C172–C173 | Vendor descriptions; no runtime reproduction. |
| Rollout history | C008–C013, C017, C027–C031 | Dated announcements/reports. |
| Replay and ML | C032–C034, C044–C045, C074–C075 | Published workflow, not private model parameters. |
| Mitigations | C014–C024, C038–C047 | Dated vendor descriptions. |
| Hallucinations implementation | C186–C190 | Vendor paper; deployed/prototype distinctions retained. |
| Boot trust and attestation | C078–C104, C110–C115 | Dated requirements and vendor statements. |
| Windows/TPM mechanisms | C133–C169, where retained | Platform documentation, not an API-use claim about RICOCHET. |
| AMD compatibility | C170–C171 | Hardware vendor advisory. |
| Input devices | C035–C036, C095–C097 | Published approach; no private thresholds. |
| LMM, reports, sanctions | C063–C073, C119–C125 | Distinct policy/investigation categories. |
| Privacy | C010, C081, C116–C117 | Vendor/legal disclosures, not a driver audit. |
| October DMA/Server Culling | C183–C185 | Visible official posts; no mechanism extrapolation. |
| Independent experiments | C221–C227 | 2023 observations, not current-build certification. |
| Artifact and research scope | C174, C180–C181, C228 | Contents verified; no cross-product transfer of findings. |
| MW4/Warzone registration and culling | C229, C232–C236 | Current support wording and attributed October interview; Warzone Season 1 remains the announced rollout scope. |
| Attestation APIs and actual sample code | C230–C231, C237–C242 | Microsoft protocol and pinned C++/PowerShell source; not proprietary game code. |

Some IDs in ranges were excluded during the strict review. Their dispositions are in [claim_review.jsonl](claim_review.jsonl). Do not fill numbering gaps with assumptions.

For current access rules, use C110–C115 with their source dates. For unconfirmed internal details, consult [review.jsonl](review.jsonl) separately. A failed integrity check, an investigation state, an account ban, and a hardware ban must retain their respective source-defined scopes.
