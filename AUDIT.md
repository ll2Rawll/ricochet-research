# Current audit decisions

The main file contains only statements located in the retrieved primary text. Labels distinguish vendor assertions, platform documentation, pinned artifacts, and historical experimental reports. This is stronger source attribution, not a claim of access to private internals.

| Finding | Action |
| --- | --- |
| C051 combined a Weekend Two figure with a beta-wide figure | Corrected text and scope; evidence cites both primary paragraphs. |
| Independent Warzone experimentation was missing | Added the CheckMATE paper, author repository, experiment dates, and methodological limits. |
| Workshop date could be confused with publication | Crossref metadata establishes November 19, 2024 publication; October dates describe the workshop. |
| Many statements were reasonable interpretations rather than direct confirmation | Separated 15 analytical entries from the main file. |
| Randgrid report claimed concrete private mechanisms/counts | Separated five entries; no build-identified reproduction supports promoting them to confirmed implementation facts. |
| Reddit returned HTTP 200 but no AMA content | Rejected the block page as source confirmation and separated six dependent entries. Earlier rendered answers are not declared false. |
| Unknowns could be mistaken for implementation facts by an AI | Moved 16 research gaps to the separate review file. |
| Three contextual comparisons duplicated retained statements | Removed duplicates from the strict file while preserving their review history. |
| Vendor success metrics could sound independently measured | Retained attribution and historical cohort scope; no independent accuracy claim. |
| General Windows capabilities could be mistaken for actual RICOCHET API use | Kept explicit platform-context labels; removed speculative runtime conclusions. |
| Long social posts were only partially visible | Used only the publisher oEmbed excerpt, with truncation recorded in source metadata. |
| Old and new access rules differ | Preserved dated/title-specific statements rather than asserting one timeless rule. |

The independent study’s reported 2023 lack of hardware-ID bans does not establish current policy. Its observation scope is explicit; later Activision policy is retained as a separate dated vendor statement. The study also excludes advanced server-side behavioral defenses and uses a bounded response window, so it is not a comprehensive current RICOCHET evaluation.

Signature counts, IOCTL counts, scan schedules, private callback/handle policies, model parameters, and hardware-identity algorithms remain unconfirmed here. Neither import presence nor a filename match establishes runtime behavior. The source-supported filename classifier is retained only as an artifact-content statement.

The disposition ledger covers all 220 original IDs; the separate review file preserves excluded material with reasons. Earlier revisions remain available in Git. No unsupported claim was silently upgraded merely because a page was readable or repeated another source.
