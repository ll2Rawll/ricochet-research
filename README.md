# RICOCHET research

Public-source research on Call of Duty’s RICOCHET anti-cheat, prepared on **2026-10-31**.

Start with **[ricochet.jsonl](ricochet.jsonl)**. It is a standalone UTF-8 knowledge file: one metadata record, source records, then compact claim records. Each claim has a stable ID, topic, evidence status, date, scope, source IDs, and a source-section locator. An AI can load it line by line or retrieve only the relevant topics.

The file separates Activision’s statements from independent observations, general Windows mechanisms, inference, and unknowns. RICOCHET is proprietary: no public-source collection can honestly specify every current driver behavior or detection rule.

The final collection contains **220 claims and research-gap notes across 58 sources**, covering 2021–October 2026. It is approximately 100 KB before supporting documentation. JSONL is streamable and easy to filter; it does not guarantee a particular model’s reading speed.

- [GUIDE.md](GUIDE.md): architecture map and reading order.
- [SOURCES.md](SOURCES.md): linked bibliography and source types.
- [index.json](index.json): topic/ID lookup and UTF-8 byte offsets.
- [METHODS.md](METHODS.md): research method and evidence labels.
- [AUDIT.md](AUDIT.md): contradictions, caveats, and excluded claims.
- [REVISION_LOG.md](REVISION_LOG.md): three substantive research passes, preserved in Git.
- [schema.json](schema.json): JSON Schema for individual records.

Retrieve a focused, citation-complete subset with standard Python:

```sh
python tools/query.py --topic kernel
python tools/query.py --search attestation --limit 8
python tools/query.py --status independent_observation
python tools/query.py --id C211
python tools/validate.py
```

`--topic`, `--status`, and `--id` can be repeated. Filters combine with AND across categories and OR within a category. Evidence dependencies and historical cross-links are included automatically, so a result may contain additional claims beyond `--limit`. No external packages or API keys are required.

For AI use, keep the metadata and evidence labels attached, preserve game/date scope, and resolve claim citations before answering. Unknowns link through `basis_claims` to nearby published evidence. Prefer the file or a retrieved subset to asking an AI to follow all links from scratch.

This repository contains original summaries and links, not redistributed game binaries, source-code leaks, or full copied articles. Historical dates matter: protections and requirements differ between games, seasons, platforms, and playlists.
