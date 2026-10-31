# Validation record

Checked on 2026-10-31 for revision 3:

- All 279 JSONL records pass the bundled Draft 2020-12 JSON Schema using `jsonschema`.
- 58 sources and 220 claim/research-gap records match the metadata and manifest.
- All source citations, evidence dependencies, and historical cross-links resolve; IDs are unique.
- General Windows context uses platform-documentation sources; unknowns retain original-audit status and evidence links.
- All 278 indexed source/claim byte ranges decode to the expected record ID.
- Retrieval checked for kernel topic, case-insensitive search with a limit, independent observations, an attestation unknown with dependencies, a current requirement with historical links, and no matches.
- Invalid topics, nonexistent claim IDs, and a zero limit fail with a clear CLI usage error.
- `git diff --check` passes.

These checks establish document and retrieval consistency. They do not validate Activision’s internal implementation, reproduce independent binary analysis, measure anti-cheat effectiveness, or guarantee that linked pages will remain unchanged. No driver was run or tested.

The manifest records the SHA-256 digest of the exact knowledge file. Regenerate derived files after editing it:

```sh
python tools/build_index.py
python tools/validate.py
```

Both commands use only Python’s standard library. JSON Schema validation was an additional preparation-time check; `jsonschema` is not needed to read, query, index, or run the repository’s integrity validator.
