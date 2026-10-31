# Research method and limits

Research date: **2026-10-31**. This is a bounded public-source literature review, not a reverse-engineering experiment or a claim of exhaustive internet coverage. No game/anti-cheat binaries, leaked code, or cheat tools were downloaded or executed. No access to Activision’s internal systems was used.

The collection prioritizes Activision support documents, dated Team RICOCHET posts, the vendor’s research whitepaper, Microsoft platform documentation, AMD’s advisory, and identifiable first-hand research. News articles were useful for discovery; technical assertions were traced back to primary material. Translations/mirrors of the same article were not counted as independent corroboration.

Searches covered the 2021 announcement and rollout, 2022–2026 seasonal updates, kernel-driver analysis, Randgrid/Brynhildr naming, replay/behavioral models, Hallucinations, false-positive incidents, hardware bans, DMA, Server Culling, TPM/Secure Boot, remote attestation, privacy, appeals, and firmware compatibility. Representative queries included:

- `site:callofduty.com/blog RICOCHET kernel driver`
- `site:callofduty.com/blog RICOCHET 2026`
- `RICOCHET kernel anti cheat technical analysis research`
- `Randgrid ricochet technical`
- `atvi-brynhildr.sys ricochet analysis`
- `RICOCHET false positives workaround October 2024`
- `site:research.activision.com hallucinations`
- `site:learn.microsoft.com Azure Attestation TPM measured boot`
- `Server Culling Call of Duty official`
- `site:amd.com TPM attestation`

Public social posts that could not be read through X directly were checked through X/Twitter’s publisher oEmbed service. Where that service truncated a long post, only its visible text was used. The source records identify those limitations. For these posts, `https://publish.twitter.com/oembed?url=<URL-encoded-post-URL>` was the retrieval endpoint. News reports’ extra details were not silently substituted for the missing primary text.

GitHub research and filename rules use pinned commit URLs; notes identify inspected blob SHAs. The independent Randgrid report was assessed as an author-reported analysis, not reproduced behavior. Import-table presence was kept separate from demonstrated calls and runtime observations. Exact quantities from that report remain attributed claims.

Each source record gives a publication/update date when known, access date, kind, URL, and reviewed sections. A null publication date means it was not reliably established; a year/month date preserves the available precision. `as_of` dates are the statement’s date or the review date, not proof that a mechanism was first deployed then or remains active everywhere today. Some sources contain announcements rather than implementation evidence.

`official_statement` means the vendor made the claim, not that this review independently verified it. `independent_observation` includes attributed first-hand research that was not reproduced here. `platform_context` describes an OS facility, never automatically a RICOCHET implementation. `inference` identifies synthesis. `qualified_interpretation` preserves a caveat or narrows a source statement.

`unknown` records are original research-gap findings. Their empty `sources` arrays prevent attributing an absence claim to a publisher. Their `basis_claims` point to the nearest published evidence so an AI can inspect what is and is not supported. Missing confirmation is not proof a technique is absent. Claims and unknowns are intentionally kept in one searchable file.

Original summaries are concise; full copyrighted articles and source-page snapshots are not included. A line-by-line data validator checks IDs, citation/dependency integrity, date formats, evidence-category boundaries, counts, and summary length. That validation cannot establish whether a publisher’s claims are true, whether a mutable webpage has changed, or whether a driver’s runtime behavior matches its public description.
