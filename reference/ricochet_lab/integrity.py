"""Scoped SHA-256 manifests for files owned by this demonstration.

This is ordinary local file integrity checking, not a trusted measurement agent.
The caller must obtain the expected manifest through a trusted channel. A user
who controls this interpreter or manifest can alter the result. Only explicitly
listed paths are read; unlisted files are neither scanned nor classified.

On platforms supporting directory-relative opens, traversal uses anchored
directory descriptors and rejects symlinks. The portable fallback checks paths
before and after opening, but cannot eliminate directory-replacement races.
Neither approach makes a collection of files an atomic filesystem snapshot.
"""

from __future__ import annotations

import errno
import hashlib
import os
from pathlib import Path
import re
import stat
from typing import Any, Iterable

MAX_FILES = 1024
MAX_FILE_BYTES = 16 * 1024 * 1024
MAX_TOTAL_BYTES = 128 * 1024 * 1024
MAX_PATH_BYTES = 1024
_HASH = re.compile(r"[0-9a-f]{64}\Z")
_WINDOWS_DEVICE = re.compile(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?\Z", re.IGNORECASE)


class IntegrityError(ValueError):
    """Unsafe path, invalid manifest, unstable file, or exhausted read budget."""


def _root_path(root: os.PathLike[str] | str) -> Path:
    try:
        result = Path(root).resolve(strict=True)
        if not result.is_dir():
            raise IntegrityError("root must be a directory")
        return result
    except (OSError, TypeError, RuntimeError) as exc:
        raise IntegrityError("root must be an accessible directory") from exc


def _relative_path(value: Any) -> str:
    if not isinstance(value, str) or not value:
        raise IntegrityError("path must be a nonempty relative POSIX path")
    try:
        length = len(value.encode("utf-8"))
    except UnicodeEncodeError as exc:
        raise IntegrityError("path must be valid UTF-8") from exc
    if length > MAX_PATH_BYTES:
        raise IntegrityError("path is too long")
    # One representation on every host: no drive letters, UNC paths, backslash
    # aliases, empty components, alternate streams, or dot traversal.
    if "\\" in value or ":" in value or "\x00" in value:
        raise IntegrityError("path contains a forbidden character")
    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise IntegrityError("path contains an unsafe component")
    # Windows normalizes trailing spaces/dots and reserves device basenames.
    # Reject these aliases even on Unix so one manifest retains one meaning.
    if any(part.endswith((" ", ".")) or _WINDOWS_DEVICE.fullmatch(part) for part in parts):
        raise IntegrityError("path contains a platform-dependent alias")
    return value


def _check_components(root: Path, relative: str) -> Path:
    current = root
    for part in relative.split("/"):
        current /= part
        info = current.lstat()
        if stat.S_ISLNK(info.st_mode):
            raise IntegrityError("symlinks are not allowed")
    try:
        current.resolve(strict=True).relative_to(root)
    except (ValueError, RuntimeError) as exc:
        raise IntegrityError("path leaves the root") from exc
    return current


def _open_scoped(root: Path, relative: str) -> tuple[int, bool]:
    anchored = (
        os.open in os.supports_dir_fd
        and hasattr(os, "O_DIRECTORY")
        and hasattr(os, "O_NOFOLLOW")
    )
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0)
    # Prevent named pipes from hanging an integrity scan before fstat rejects
    # them. Regular-file reads are unaffected by O_NONBLOCK.
    flags |= getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0)
    try:
        if not anchored:
            path = _check_components(root, relative)
            descriptor = os.open(path, flags)
            try:
                _check_components(root, relative)
            except BaseException:
                os.close(descriptor)
                raise
            return descriptor, False
        directory_flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        directory = os.open(root, directory_flags)
        try:
            parts = relative.split("/")
            for part in parts[:-1]:
                child = os.open(part, directory_flags, dir_fd=directory)
                os.close(directory)
                directory = child
            return os.open(parts[-1], flags, dir_fd=directory), True
        finally:
            os.close(directory)
    except OSError as exc:
        if exc.errno in (errno.ELOOP, errno.ENOTDIR):
            raise IntegrityError("symlink or non-directory path component") from exc
        raise


def _hash_entry(root: Path, relative: str, budget: list[int]) -> dict[str, Any]:
    descriptor, anchored = _open_scoped(root, relative)
    try:
        before = os.fstat(descriptor)
        if not stat.S_ISREG(before.st_mode):
            raise IntegrityError("only regular files can be measured")
    except BaseException:
        os.close(descriptor)
        raise
    with os.fdopen(descriptor, "rb") as stream:
        if before.st_size > MAX_FILE_BYTES:
            raise IntegrityError("file exceeds the size limit")
        if before.st_size > budget[0]:
            raise IntegrityError("total read budget exceeded")
        digest = hashlib.sha256()
        size = 0
        while chunk := stream.read(min(65536, budget[0] + 1)):
            size += len(chunk)
            budget[0] -= len(chunk)
            if size > MAX_FILE_BYTES or budget[0] < 0:
                raise IntegrityError("read budget exceeded while reading")
            digest.update(chunk)
        after = os.fstat(stream.fileno())
        if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
            after.st_size, after.st_mtime_ns, after.st_ctime_ns
        ) or size != after.st_size:
            raise IntegrityError("file changed during measurement")
    if not anchored:
        _check_components(root, relative)
    return {"path": relative, "sha256": digest.hexdigest(), "size": size}


def build_manifest(
    root: os.PathLike[str] | str, relative_paths: Iterable[str]
) -> dict[str, Any]:
    """Hash a bounded explicit allowlist; fail if any entry cannot be measured."""
    directory = _root_path(root)
    if isinstance(relative_paths, (str, bytes)):
        raise IntegrityError("relative_paths must be an iterable of paths")
    paths: set[str] = set()
    for value in relative_paths:
        path = _relative_path(value)
        if path in paths:
            raise IntegrityError("duplicate path")
        paths.add(path)
        if len(paths) > MAX_FILES:
            raise IntegrityError("too many files")
    budget = [MAX_TOTAL_BYTES]
    return {
        "version": 1,
        "files": [_hash_entry(directory, path, budget) for path in sorted(paths)],
    }


def _validated_entries(manifest: Any) -> list[dict[str, Any]]:
    if not isinstance(manifest, dict) or set(manifest) != {"version", "files"}:
        raise IntegrityError("manifest must contain exactly version and files")
    if type(manifest["version"]) is not int or manifest["version"] != 1:
        raise IntegrityError("unsupported manifest version")
    entries = manifest["files"]
    if not isinstance(entries, list) or len(entries) > MAX_FILES:
        raise IntegrityError("files must be a bounded list")
    paths: set[str] = set()
    total = 0
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {"path", "sha256", "size"}:
            raise IntegrityError("invalid file record")
        path = _relative_path(entry["path"])
        if path in paths:
            raise IntegrityError("duplicate path")
        paths.add(path)
        if not isinstance(entry["sha256"], str) or not _HASH.fullmatch(entry["sha256"]):
            raise IntegrityError("invalid SHA-256 digest")
        size = entry["size"]
        if type(size) is not int or not 0 <= size <= MAX_FILE_BYTES:
            raise IntegrityError("invalid file size")
        total += size
        if total > MAX_TOTAL_BYTES:
            raise IntegrityError("manifest exceeds the total size limit")
    return entries


def verify_manifest(
    root: os.PathLike[str] | str, manifest: Any
) -> dict[str, Any]:
    """Measure listed files against a separately trusted expected manifest.

    Schema errors raise IntegrityError; per-file failures are reported in
    ``errors``. A mismatch is an integrity observation, not proof of cheating.
    ``checked`` counts files successfully measured, including modified files.
    """
    directory = _root_path(root)
    entries = _validated_entries(manifest)
    report: dict[str, Any] = {
        "version": 1, "ok": True, "checked": 0,
        "missing": [], "modified": [], "errors": [],
    }
    budget = [MAX_TOTAL_BYTES]
    for expected in sorted(entries, key=lambda item: item["path"]):
        path = expected["path"]
        try:
            actual = _hash_entry(directory, path, budget)
        except FileNotFoundError:
            report["missing"].append(path)
        except IntegrityError as exc:
            report["errors"].append({"path": path, "reason": str(exc)})
        except OSError:
            report["errors"].append({"path": path, "reason": "file could not be read"})
        else:
            report["checked"] += 1
            if actual["sha256"] != expected["sha256"] or actual["size"] != expected["size"]:
                report["modified"].append(path)
    report["ok"] = not (report["missing"] or report["modified"] or report["errors"])
    return report
