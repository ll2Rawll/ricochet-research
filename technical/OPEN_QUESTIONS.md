# Remaining technical questions

These are research boundaries, not confirmed implementation claims. Reviewed 2026-10-31.

- **Current kernel build:** no publisher-identified MW4/Warzone driver sample was independently inspected here. Exact callback usage, dynamically resolved functions, code-integrity checks and scan cadence remain unverified.
- **Driver communication:** no current device interface, private IOCTL schema, message authentication/encryption design or complete user/kernel call graph was reproduced.
- **Remote policy:** the public protocol does not disclose Activision's relying-party configuration, production nonce lifecycle, PCR policy, token-binding validation, endpoint topology or re-attestation schedule.
- **Enrollment and enforcement:** public helper strings do not establish that an AIK, TPM endorsement identity or any particular combination of hardware values is the hardware-ban database key.
- **Server Culling:** visibility rules, map preprocessing, prediction/hysteresis, relevancy exceptions, update cadence and current per-map deployment require further primary evidence. The Warzone Season 1 announcement should not be rewritten as an already measured deployment.
- **DMA:** the October announcement supplies no reproducible detection algorithm, device fingerprint list or proof of a mandatory IOMMU policy.
- **Hallucinations:** the 2023 paper's concrete serialization design does not certify present MW4/Warzone code or the continued use of every described behavior.
- **Effectiveness:** no current controlled experiment here measures detection rates, false positives, performance cost or evasions. The CheckMATE study preserves its 2023 test dates.

## Leads excluded from the confirmed technical file

| Lead | Why it remains outside the confirmed file |
| --- | --- |
| [Hash-identified October Randgrid report](https://drivershield.io/DS-3F9CC393CCA7/54a28531aa42de4f7b400025b2512d9dfee0cc90401e38a446dfcc820af8522f) | Third-party metadata/import/control-code claims were not reproduced from a trusted-source sample. Its hash does not establish a verified MW4 game-build mapping. An automated risk label does not establish malicious intent or an exploitable vulnerability. |
| [Hash-identified August Randgrid report](https://drivershield.io/DS-D6F454AA9717/452e46ff38ac24d5c4763873430770cd680ba0b472a903c7b05641d8173e13cd) | Same provenance/reproduction limits; not transferable to a later driver build. |
| [Pinned Valkyrie README](https://github.com/AmenHermano144/Valkyrie-Complete-Reverse-Engineering-Analysis/blob/d148d69b77e86b78101c322bed34c9dac78c4758/README.md) | Earlier review excluded its kernel counts and derived mechanisms. Matching some helper literals in the official diagnostic artifact does not validate its kernel analysis or hardware-ban reconstruction. |
| [Official Server Culling video](https://www.youtube.com/watch?v=nYN3MPRuznU) | Direct video retrieval was throttled. No unverified transcript or commentary-derived visibility algorithm was promoted to a fact. The independently retrieved vendor interview supplies a narrower statement. |

A stronger follow-up would identify an authentic publisher-distributed driver/game build, record hashes and signature verification, and attach function/control-flow evidence to each proposed mechanism. The official diagnostic utility provides this level of file identity for its own components only.
