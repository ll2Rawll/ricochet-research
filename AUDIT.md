# Revision 3 audit decisions

| Issue | Resolution in the knowledge file |
| --- | --- |
| “Exactly how it works” exceeds public disclosure | Keep an explicit implementation-gap register, C204–C219. |
| Older FAQ says no additional driver requirements | Preserve the text’s scope; newer title-specific security rules take priority for game access, C191–C193. |
| Optional 2025 TPM test versus later requirements | Retain historical rollout dates and cross-links rather than flattening them. |
| BO7/Warzone versus MW4 noncompliance | Separate restricted playlists from the all-online MW4 restriction. |
| Failed attestation treated as proof of cheating | Distinguish compatibility failures, LMM investigations, and confirmed sanctions, C202. |
| ML assistance treated as fully autonomous banning | Preserve the published review role and avoid claims about every enforcement path, C195. |
| Every imported kernel API called “confirmed detection” | Attribute independent observations and mark import-derived behavior as unverified, C175–C179. |
| Cold War TAC research used as kernel-driver documentation | Keep it as scoped context, C180. |
| Academic rootkit study misapplied to RICOCHET | Identify its actual case studies, C181. |
| Driver filename treated as authenticity proof | Keep filename association distinct from signature/hash/build verification, C174. |
| Exact hardware-ban algorithm inferred from TPM enrollment | Confirm the enforcement category; preserve identifier uncertainty, C212. |
| October 2024 false bans | Include the official admission/restoration. Do not claim the vendor confirmed the alleged underlying string-scanning implementation, C182. |
| New October 2026 DMA/Server Culling reports | Verify visible official posts through publisher oEmbed; exclude unverified low-level implementation and coverage details. |
| “No cheaters” inferred from ban counts | Preserve cohort/time windows; flag absent precision/recall and undetected-population data, C196/C219. |
| Privacy claims flattened into technical impossibility | Separate vendor purpose statements, broad legal disclosures, actual observed behavior, and unknowns. |

Discovery leads excluded from implementation facts include cheat-seller marketing, copied capability lists without provenance, filename-only malware ratings, source-leak rumors, and community troubleshooting anecdotes. No claim that these were exhaustively evaluated is made. An automated risk score or unsigned community assertion is not a substitute for build-specific evidence.

The general Windows examples are explanatory context. They do not assert that RICOCHET registers a given callback, reads every physical page, operates a VBS enclave, or enforces every security feature supported by Microsoft’s attestation service.

Source dates reflect available precision. The Hallucinations paper’s exact March 30, 2023 date was added in this revision. The April 4, 2023 report’s URL contains March; its displayed publication date is retained. Current support pages may change after this review.
