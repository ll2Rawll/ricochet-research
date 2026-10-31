# Validation record

Revision 6, reviewed on 2026-10-31:

- The core remains 197 statements from 64 sources; its metadata revision is 6.
- The focused technical file has 104 records: one metadata record, 29 sources (including one diagnostic artifact), 58 core entries and 16 static observations.
- Both datasets pass their JSON Schemas. Core citations/paragraphs/index and focused citations/report pointers/build hashes pass integrity checks.
- Technical query cases cover observation IDs, status/topic filters, citation closure, subset counts, empty matches and invalid inputs.
- Twelve format/malformed-input checks pass for the read-only bundle parser, including truncation, traversal, duplicate/missing entries, size bounds, DEFLATE overexpansion and archive hash rejection.
- The official ZIP was downloaded again through the checked-in fetcher and matched the reviewed SHA-256. A fresh virtual environment installed the pinned requirements and reproduced the static report byte for byte.
- Certificate metadata was parsed; signature trust was not verified. No contained Windows code, game, enrollment command or kernel driver was executed.

~~~sh
python tools/build_index.py
python tools/build_technical.py
python tools/validate.py
python tools/validate_technical.py
python tools/test_static_inspector.py
~~~

See [MW4_WARZONE_TECHNICAL.md](MW4_WARZONE_TECHNICAL.md) for the artifact reproduction commands. Dedicated artifact parsers require the optional pinned packages; normal queries and integrity checks use standard Python.

Revision 5, reviewed on 2026-10-31:

- 262 core records: one metadata record, 64 sources, and 197 supported entries.
- Four new primary-text retrievals have response hashes and paragraph locators; the complete ledger has 66 attempts.
- New code summaries retain Microsoft sample scope and commit pins. Interview summaries retain vendor attribution and announced rollout dates.
- Core integrity, schema, citation closure, query filtering and byte-index checks pass.

Earlier validation:

Revision 4, reviewed on 2026-10-31:

- 244 main-file records: one metadata record, 60 sources, and 183 supported entries.
- Every main-file entry has primary-text paragraph locators and a source-statement-support result.
- Source verification hashes and paragraph counts agree with the retrieval ledger.
- The source ledger records 62 attempts. One HTTP-200 Reddit block page was explicitly rejected as content verification.
- All original 220 entries have a review disposition. Forty-five excluded entries are disjoint from the main file.
- Schema, citation closure, historical cross-links, uniqueness, date formats, and the UTF-8 byte index are checked.
- Query checks cover topic filtering, case-insensitive search, historical links, independent-study reports, empty results, and invalid arguments.
- Evidence extraction can be reproduced for the driver FAQ and the independent research PDF against their recorded normalization hashes.

These checks establish document/retrieval consistency and traceability. They do not prove Activision’s claims, reproduce a driver experiment, or establish current anti-cheat precision/recall. Primary-text support was assessed by reviewing the located passages; a passing script alone is not factual confirmation.

~~~sh
python tools/build_index.py
python tools/validate.py
python tools/fetch_evidence.py --source S01
~~~

The optional fetch tool stores source snapshots in an ignored local .evidence-cache directory. Research-PDF extraction additionally requires pdftotext from Poppler. No game driver or cheat executable is fetched or run. JSON Schema validation uses jsonschema as a preparation-time check; the repository’s query/index/validator tools require only standard Python.
