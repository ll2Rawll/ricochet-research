# Confirmation method

The strict main file, [ricochet.jsonl](ricochet.jsonl), contains **183 source-supported entries from 60 sources**. Every retained entry has primary-text paragraph locators and a verification record. Every original entry has a disposition in [claim_review.jsonl](claim_review.jsonl).

**Confirmation means the retrieved primary text supports the attributed statement.** It does not independently establish a vendor’s accuracy, runtime behavior, performance, or universal coverage. Vendor statements, documented platform facilities, pinned artifacts, and historical experimental reports have separate labels. No driver experiment was performed here.

The deeper pass attempted all 58 earlier sources and four additional sources: the CheckMATE paper, its authors’ repository, RFC 7519, and the paper’s DOI registration. All 62 requests returned HTTP 200, but **the Reddit response was a block page rather than AMA text**. HTTP success alone was rejected as confirmation. The six AMA-dependent entries were excluded from the strict main file. Earlier rendered answers remain in the prior history; this is a re-verification limit, not evidence that the answers were false.

The independent Randgrid report was readable, but its findings were not reproduced against a build-identified sample. Its five related entries were excluded from the main file. Imports, possible techniques, and reported command/signature counts are not treated as current implementation facts.

The 45 items in [review.jsonl](review.jsonl) comprise 16 research gaps, 15 analytical interpretations, five Randgrid-report items, six AMA-dependent items without repeat direct verification, and three duplicates. This is an audit/research file, **not confirmed facts**. Gaps in main-file IDs are intentional.

Primary sources were retrieved directly over HTTPS; pinned GitHub files used its raw-content host. Four official X posts used the publisher oEmbed endpoint. Only visible text was used when long posts were truncated. News reports supplied discovery leads, not low-level implementation proof.

[source_checks.json](source_checks.json) records URLs, retrieval methods, HTTP status, byte counts, hashes, and content-validation outcomes for all 62 attempts. No full articles, game binaries, leaked code, or cheat programs are redistributed. A source hash identifies the reviewed response, not its truth or future immutability.

Paragraph IDs such as P0085 refer to normalized extraction, not the website’s own line numbering. HTML block boundaries become nonempty paragraphs; research PDFs use pdftotext -layout. Source records include the normalized-text hash and paragraph count. The optional evidence-fetching tool regenerates local review files for comparison. A changed hash requires review; it does not automatically mean a factual change.

Publication/update dates preserve available precision. Crossref registers the CheckMATE paper’s publication as November 19, 2024, distinct from the October workshop dates in the paper. Its experiments occurred in 2023. A statement’s date is not necessarily its first-deployment date or evidence it remains active everywhere.

Microsoft/AMD documentation explains platform behavior. It does not establish RICOCHET’s actual private API selection, scan schedule, attestation policy, hardware identifiers, or model parameters. Those remain outside the confirmed implementation scope.

Automated checks establish schema, citation, evidence-location, ledger, and index consistency. **They do not automatically prove factual truth.** Retained summaries were reviewed against their located primary passages.
