#!/usr/bin/env python3
"""Check research records, citations, and archived-file integrity without execution."""

from __future__ import annotations

import hashlib
import json
import re
import sys
import xml.etree.ElementTree as ET
from datetime import date
from html.parser import HTMLParser
from pathlib import Path, PurePosixPath
from urllib.parse import urlsplit


ROOT = Path(__file__).resolve().parents[1]
ARTIFACT = ROOT / "artifacts/laith-km-driver"
REVIEW = ROOT / "research/laith-km-driver-review/review.json"


class ValidationError(Exception):
    """A research consistency check failed."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def read_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValidationError(f"{path.relative_to(ROOT)}: {exc}") from exc


def repository_file(value: str) -> Path:
    require(isinstance(value, str), "Repository file paths must be strings")
    relative = PurePosixPath(value)
    require(not relative.is_absolute() and ".." not in relative.parts,
            f"Invalid repository-relative path: {value!r}")
    path = ROOT.joinpath(*relative.parts)
    require(path.resolve().is_relative_to(ROOT), f"Path leaves repository: {value}")
    require(path.is_file(), f"Missing file: {value}")
    return path


def check_reference_list(value, known: set[str], location: str) -> None:
    require(isinstance(value, list) and all(isinstance(x, str) for x in value),
            f"{location}: expected a list of IDs")
    require(len(value) == len(set(value)), f"{location}: duplicate IDs")
    missing = set(value) - known
    require(not missing, f"{location}: unresolved IDs {sorted(missing)}")


def check_nested_references(value, sources: set[str], claims: set[str], location: str) -> None:
    if isinstance(value, dict):
        for key, item in value.items():
            child = f"{location}.{key}"
            if key == "src" or key.endswith("source_ids"):
                check_reference_list(item, sources, child)
            elif key.endswith("claim_ids"):
                check_reference_list(item, claims, child)
            elif key.endswith("source_id") or key == "scope_source":
                require(item in sources, f"{child}: unresolved source {item!r}")
            elif key == "at" and isinstance(item, dict):
                require(set(item) <= sources, f"{child}: unresolved paragraph source")
            check_nested_references(item, sources, claims, child)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            check_nested_references(item, sources, claims, f"{location}[{index}]")


def check_records():
    path = ROOT / "ricochet.jsonl"
    records = []
    try:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            require(bool(line.strip()), f"ricochet.jsonl:{number}: blank record")
            try:
                record = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValidationError(f"ricochet.jsonl:{number}: {exc}") from exc
            require(isinstance(record, dict), f"ricochet.jsonl:{number}: expected object")
            records.append(record)
    except (OSError, UnicodeError) as exc:
        raise ValidationError(f"ricochet.jsonl: {exc}") from exc
    require(bool(records) and records[0].get("type") == "meta", "First record must be metadata")
    require(sum(row.get("type") == "meta" for row in records) == 1,
            "Expected exactly one metadata record")
    meta = records[0]
    rows = records[1:]
    require(all(row.get("type") in {"source", "claim"} for row in rows),
            "Records after metadata must be sources or claims")
    identifiers = [row.get("id") for row in rows]
    require(all(isinstance(identifier, str) and identifier for identifier in identifiers),
            "Every source and claim requires a nonempty ID")
    require(len(identifiers) == len(set(identifiers)), "Duplicate source/claim IDs")
    sources = {row["id"] for row in rows if row["type"] == "source"}
    claims = {row["id"] for row in rows if row["type"] == "claim"}
    counts = {
        "sources": len(sources),
        "claims": len(claims),
        "source_supported_claims": sum(identifier.startswith("C") for identifier in claims),
        "static_artifact_observations": sum(identifier.startswith("T") for identifier in claims),
        "uploaded_artifact_observations": sum(identifier.startswith("F") for identifier in claims),
    }
    for field, count in counts.items():
        require(meta.get(field) == count,
                f"Metadata {field}: expected {count}, found {meta.get(field)!r}")
    for row in rows:
        if row["type"] == "claim":
            require(bool(row.get("src")), f"{row['id']}: missing source citations")
    for index, row in enumerate(records):
        check_nested_references(row, sources, claims, f"record[{index}]")
    return meta, sources, claims


def check_readme(meta, sources: set[str], claims: set[str]) -> None:
    text = (ROOT / "README.md").read_text(encoding="utf-8")
    for label, field in (("FINDINGS", "claims"), ("SOURCES", "sources")):
        require(f"/badge/{label}-{meta[field]}-" in text,
                f"README {label} badge does not match metadata")
    require(f"{meta['claims']} findings and {meta['sources']} sources" in text,
            "README global search totals do not match metadata")
    for source in sources:
        definitions = re.findall(rf"^\[\^{re.escape(source)}\]:", text, re.MULTILINE)
        require(len(definitions) == 1, f"README requires one footnote definition for {source}")
    for claim in claims:
        anchor = rf'<a id="{re.escape(claim.lower())}"></a>'
        row = rf"^\|\s*{anchor}\*\*\[{re.escape(claim)}\]\([^\n)]*#{re.escape(claim)}\)"
        require(len(re.findall(row, text, re.MULTILINE)) == 1,
                f"README requires one linked catalogue row for {claim}")


def check_manifest() -> int:
    manifest = read_json(ARTIFACT / "manifest.json")
    entries = manifest.get("included_files", [])
    require(bool(entries), "Artifact manifest contains no imported files")
    expected = {}
    total_bytes = 0
    for entry in entries:
        path = repository_file(entry["repository_path"])
        require(path.is_relative_to(ARTIFACT / "source"),
                f"Imported file outside source snapshot: {entry['repository_path']}")
        content = path.read_bytes()
        require(len(content) == entry["bytes"], f"Size mismatch: {entry['repository_path']}")
        digest = hashlib.sha256(content).hexdigest()
        require(digest == entry["sha256"], f"SHA-256 mismatch: {entry['repository_path']}")
        relative = path.relative_to(ARTIFACT).as_posix()
        require(relative not in expected, f"Duplicate manifest file: {relative}")
        expected[relative] = digest
        total_bytes += len(content)
    listed = {}
    for number, line in enumerate((ARTIFACT / "SHA256SUMS").read_text(encoding="utf-8").splitlines(), 1):
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        require(match is not None, f"SHA256SUMS:{number}: malformed checksum entry")
        digest, path = match.groups()
        require(path not in listed, f"SHA256SUMS:{number}: duplicate file")
        listed[path] = digest
    require(listed == expected, "SHA256SUMS does not match the original-file manifest")
    summary = manifest.get("summary", {})
    if "included_file_count" in summary:
        require(summary["included_file_count"] == len(entries), "Manifest imported-file count mismatch")
    if "included_bytes" in summary:
        require(summary["included_bytes"] == total_bytes, "Manifest imported-byte count mismatch")
    return len(entries)


def local_tag(element) -> str:
    return element.tag.rsplit("}", 1)[-1]


def check_project(project: dict) -> None:
    value = project.get("path")
    require(isinstance(value, str), "Review project requires a file path")
    if not value.startswith("artifacts/"):
        value = "artifacts/laith-km-driver/source/" + value
    path = repository_file(value)
    require(path.is_relative_to(ARTIFACT / "source"), f"Project outside imported source: {value}")
    require(hashlib.sha256(path.read_bytes()).hexdigest() == project.get("sha256"),
            f"Review project SHA-256 mismatch: {value}")
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as exc:
        raise ValidationError(f"{value}: invalid project XML: {exc}") from exc
    configurations = sorted({element.attrib["Include"] for element in root.iter()
                             if local_tag(element) == "ProjectConfiguration"
                             and "Include" in element.attrib})
    configuration_names = sorted({item.split("|", 1)[0] for item in configurations})
    platforms = sorted({item.split("|", 1)[1] for item in configurations if "|" in item})
    for key, actual in (("platforms", platforms),
                        ("driver_types", sorted({element.text.strip() for element in root.iter()
                                                 if local_tag(element) == "DriverType" and element.text})),
                        ("platform_toolsets", sorted({element.text.strip() for element in root.iter()
                                                      if local_tag(element) == "PlatformToolset" and element.text}))):
        if key in project:
            require(isinstance(project[key], list) and sorted(project[key]) == actual,
                    f"{value}: {key} does not match project XML")
    if "configurations" in project:
        require(isinstance(project["configurations"], list)
                and sorted(project["configurations"]) in (configurations, configuration_names),
                f"{value}: configurations do not match project XML")
    references = [element.attrib["Include"] for element in root.iter()
                  if local_tag(element) in {"ClCompile", "ClInclude", "MASM", "Inf"}
                  and "Include" in element.attrib]
    missing = []
    for reference in references:
        target = path.parent / reference.replace("\\", "/")
        require(target.resolve().is_relative_to(ARTIFACT / "source"),
                f"{value}: project source reference leaves snapshot: {reference}")
        if not target.is_file():
            missing.append(reference)
    require(not missing, f"{value}: missing explicit project files: {missing}")
    if "project_file_reference_count" in project:
        require(project["project_file_reference_count"] == len(references),
                f"{value}: project-file reference count mismatch")
    if "missing_project_file_references" in project:
        require(project["missing_project_file_references"] == missing,
                f"{value}: missing-file metadata mismatch")
    absolute_count = 0
    for element in root.iter():
        if local_tag(element) == "AdditionalIncludeDirectories" and element.text:
            for entry in element.text.split(";"):
                if re.match(r"^(?:[A-Za-z]:[\\/]|\\\\)", entry.strip()):
                    absolute_count += 1
    if "absolute_include_path_entry_count" in project:
        require(project["absolute_include_path_entry_count"] == absolute_count,
                f"{value}: absolute include-path count mismatch")


def check_review(sources: set[str], claims: set[str]) -> int:
    review = read_json(REVIEW)
    check_nested_references(review, sources, claims, "review")
    if "artifact_commit" in review:
        require(isinstance(review["artifact_commit"], str)
                and re.fullmatch(r"[0-9a-f]{40}", review["artifact_commit"]) is not None,
                "Review artifact_commit must be a full Git SHA")
    projects = review.get("projects", [])
    require(isinstance(projects, list), "Review projects must be a list")
    for project in projects:
        require(isinstance(project, dict), "Review project must be an object")
        check_project(project)
    return len(projects)


class ReaderHTML(HTMLParser):
    """Collect static reader labels and locally referenced browser assets."""

    def __init__(self) -> None:
        super().__init__()
        self.labels: dict[str, str] = {}
        self.open_elements: list[tuple[str, str | None]] = []
        self.assets: list[str] = []

    def handle_starttag(self, tag: str, attrs) -> None:
        attributes = dict(attrs)
        identifier = attributes.get("id")
        if identifier:
            self.labels.setdefault(identifier, "")
        if tag not in {"area", "base", "br", "col", "embed", "hr", "img", "input",
                       "link", "meta", "param", "source", "track", "wbr"}:
            self.open_elements.append((tag, identifier))
        if tag == "script" and attributes.get("src"):
            self.assets.append(attributes["src"])
        if tag == "link" and "stylesheet" in attributes.get("rel", "").split():
            if attributes.get("href"):
                self.assets.append(attributes["href"])

    def handle_endtag(self, tag: str) -> None:
        for index in range(len(self.open_elements) - 1, -1, -1):
            if self.open_elements[index][0] == tag:
                del self.open_elements[index:]
                break

    def handle_data(self, data: str) -> None:
        for _, identifier in self.open_elements:
            if identifier:
                self.labels[identifier] += data


def check_reader(meta) -> None:
    reader = ROOT / "reader"
    snapshot = read_json(reader / "data.json")
    corpus = [json.loads(line) for line in (ROOT / "ricochet.jsonl").read_text(
        encoding="utf-8").splitlines()]
    require(snapshot == corpus, "reader/data.json differs from the JSONL research corpus")

    app = repository_file("reader/app.js").read_text(encoding="utf-8")
    revisions = re.findall(r"\bconst\s+SNAPSHOT_REVISION\s*=\s*(\d+)\s*;", app)
    require(len(revisions) == 1 and int(revisions[0]) == meta.get("revision"),
            "Reader SNAPSHOT_REVISION does not match research metadata")

    document = ReaderHTML()
    document.feed(repository_file("reader/index.html").read_text(encoding="utf-8"))
    expected = {
        "total-findings": str(meta["claims"]),
        "total-sources": str(meta["sources"]),
        "findings-tab-count": str(meta["claims"]),
        "sources-tab-count": str(meta["sources"]),
        "revision": f"Research revision {meta['revision']}",
    }
    for identifier, value in expected.items():
        require(document.labels.get(identifier, "").strip() == value,
                f"Reader static label {identifier!r} does not match research metadata")
    try:
        reviewed = date.fromisoformat(meta["reviewed"])
    except (TypeError, ValueError) as exc:
        raise ValidationError("Research reviewed date must be a valid ISO date") from exc
    actual_date = document.labels.get("reviewed", "").strip().upper()
    month = ("JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC")[reviewed.month - 1]
    require(actual_date in {f"{reviewed.day} {month} {reviewed.year}",
                            f"{reviewed.day:02} {month} {reviewed.year}"},
            "Reader static reviewed date does not match research metadata")
    require("app.js" in document.assets and "app.css" in document.assets,
            "Reader index must reference app.js and app.css")
    for asset in document.assets:
        parsed = urlsplit(asset)
        if parsed.scheme or parsed.netloc:
            continue
        require(not parsed.path.startswith("/"), f"Reader asset must be relative: {asset}")
        repository_file("reader/" + parsed.path)


def main() -> int:
    try:
        meta, sources, claims = check_records()
        check_readme(meta, sources, claims)
        imported_count = check_manifest()
        project_count = check_review(sources, claims)
        check_reader(meta)
    except (ValidationError, OSError, UnicodeError, KeyError, TypeError) as exc:
        print(f"Research validation failed: {exc}", file=sys.stderr)
        return 1
    print(f"Research validation passed: revision {meta.get('revision')}, "
          f"{len(claims)} findings, {len(sources)} sources; "
          f"{imported_count} imported-file hashes, {project_count} project metadata records "
          "and reader snapshot/labels/assets verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
