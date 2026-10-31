# RICOCHET research

Public-source research on Call of Duty’s anti-cheat, reviewed on **2026-10-31**.

Start with **[ricochet.jsonl](ricochet.jsonl)**: **197 source-supported entries from 64 sources**, with dates, scope, citations, evidence labels, paragraph locators, and verification metadata. Standalone UTF-8 JSONL, approximately 138 KB, suitable for streaming or selective AI retrieval.

**Source-confirmed is not independently proven runtime behavior.** Activision’s statements remain attributed. Platform capabilities are not proof of a RICOCHET implementation. Historical experiments retain their test dates. Unconfirmed internals are outside the main file.

- [GUIDE.md](GUIDE.md): reading map.
- [SOURCES.md](SOURCES.md): retained-source bibliography.
- [index.json](index.json): topic/ID lookup and UTF-8 byte offsets.
- [source_checks.json](source_checks.json): 66 source attempts, content checks, and hashes.
- [claim_review.jsonl](claim_review.jsonl): disposition of every original entry.
- [review.jsonl](review.jsonl): 45 excluded interpretations, gaps, unreplicated findings, unavailable verifications, and duplicates; **not confirmed facts**.
- [METHODS.md](METHODS.md): confirmation method and limits.
- [AUDIT.md](AUDIT.md): corrections and exclusions.
- [REVISION_LOG.md](REVISION_LOG.md): substantive revisions preserved in Git.
- [schema.json](schema.json): record schema version 1.1.0.

Query a focused, citation-complete subset:

~~~sh
python tools/query.py --topic kernel
python tools/query.py --search attestation --limit 8
python tools/query.py --status study_report
python tools/query.py --id C051
python tools/query.py --topic attestation_code
python tools/query.py --topic server_culling
python tools/validate.py
~~~

Repeat a topic, status, or ID option for OR within that category; different categories combine with AND. Historical cross-links are included automatically, so output can exceed the limit. Querying, indexing, and integrity validation require only standard Python.

Keep metadata, attribution, date/game scope, and evidence labels attached when supplying this file to an AI. The main statuses are official_statement, platform_context, artifact_statement, and study_report. Use the separate review file when investigating open questions.

This repository contains original summaries and references, not game binaries, leaked source, copied articles, or executed cheat programs. It does not claim exhaustive coverage or complete knowledge of Activision’s private implementation. GitHub repository: [ll2Rawll/ricochet-research](https://github.com/ll2Rawll/ricochet-research).

To regenerate a source’s paragraph-labeled evidence locally:

~~~sh
python tools/fetch_evidence.py --source S01
python tools/fetch_evidence.py --source S59
~~~

The cache is ignored by Git. Research PDF extraction requires Poppler’s pdftotext; HTML/artifact retrieval uses standard Python. Compare the reported normalized-text hash with the source record. A mismatch signals a need for review, not automatic factual invalidity.
