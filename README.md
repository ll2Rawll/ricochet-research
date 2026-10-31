# RICOCHET research

Public-source research on Call of Duty’s RICOCHET anti-cheat, prepared on **2026-10-31**.

Start with **[ricochet.jsonl](ricochet.jsonl)**. It is a standalone UTF-8 knowledge file: one metadata record, source records, then compact claim records. Each claim has a stable ID, topic, evidence status, date, scope, source IDs, and a source-section locator. An AI can load it line by line or retrieve only the relevant topics.

The file separates Activision’s statements from independent observations, general Windows mechanisms, inference, and unknowns. RICOCHET is proprietary: no public-source collection can honestly specify every current driver behavior or detection rule.

Revision history is preserved in Git. The initial pass covers the driver’s launch and lifecycle, the unified platform, replay review, and early mitigations; later passes expand and audit that foundation.

This repository contains original summaries and links, not redistributed game binaries, source-code leaks, or full copied articles. Historical dates matter: protections and requirements differ between games, seasons, platforms, and playlists.
