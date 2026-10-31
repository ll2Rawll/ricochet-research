# Validation record

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
