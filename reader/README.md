# Searchable research reader

This directory contains the complete static reader: HTML, JavaScript, CSS and a saved research snapshot. It requires no build step or package installation.

## Open locally

From the repository root:

```sh
python -m http.server 8000 --bind 127.0.0.1
```

Open `http://127.0.0.1:8000/reader/`. Use a local HTTP server rather than opening the HTML as a file, because the reader loads its dataset with `fetch`.

The reader first requests the live `main/ricochet.jsonl` dataset. If that request fails, it uses the complete bundled `data.json` snapshot and identifies it as a saved snapshot. Searches, category/evidence filters, source citations and selection exports work with either data source. The local server must be running for the bundled fallback; no service worker or offline browser cache is installed.

## Update and check

`ricochet.jsonl` is the authoritative research dataset. After editing it:

```sh
python scripts/sync_reader.py
python scripts/sync_reader.py --check
python scripts/validate_research.py
```

Synchronization refreshes `data.json`, the minimum snapshot revision and the initial HTML counts/review date. Validation checks the snapshot against the corpus, citations, project metadata and original-file hashes.

## Published reader

The repository's existing `gh-pages` branch serves a copy of `index.html`, `app.js`, `app.css`, `data.json` and `.nojekyll` from this directory. Update those five files together when publishing reader changes. The main README links to the pinned published reader version.

The project-review link and **Imported project** search connect the added archive research to the library. The reader presents research evidence; it does not execute the imported driver.
