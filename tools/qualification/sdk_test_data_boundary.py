#!/usr/bin/env python3
"""Fail-closed, offline audit: licensed original-source PDFs remain test-only.

Read a real CMake install_manifest.txt *after* installing the SDK. The
manifest is the authoritative list of installed files, unlike the original
source tree. The original TeXample PDF is licensed CC BY-SA 4.0 and must not
be redistributed as an accidental SDK test fixture. This checker deliberately
does not grant permissions or count source-pixel conformance.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys

SOURCE_SHA256 = "f691ff46e64353ad285a5ddf24c82b956de67b5a6b3ed7d244167a301f36a284"
SOURCE_FILENAME = "texample.intersecting-rings-v1.pdf"
SOURCE_BYTES = 1396
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_ENTRIES = 16384
MAX_PDF_BYTES = 64 * 1024 * 1024
REQUIRED_NOTICE = (
    "Dominique Wuertz",
    "TeXample",
    "CC BY-SA 4.0",
    "https://creativecommons.org/licenses/by-sa/4.0/",
)


class SDKDataBoundaryError(ValueError):
    """The installed SDK cannot prove test-data and license isolation."""


def _reject(message: str) -> None:
    raise SDKDataBoundaryError(message)


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(65536)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def check_install_manifest(manifest_path: Path) -> dict:
    if manifest_path.is_symlink() or not manifest_path.is_file():
        _reject("CMake install manifest missing or symlinked")
    size = manifest_path.stat().st_size
    if not 0 < size <= MAX_MANIFEST_BYTES:
        _reject("CMake install manifest exceeds budget or is empty")
    try:
        entries = manifest_path.read_text(encoding="utf-8").splitlines()
    except UnicodeError as exc:
        _reject(f"invalid UTF-8 install manifest: {exc}")
    if not 0 < len(entries) <= MAX_ENTRIES:
        _reject("invalid installed file count")

    notice_path = None
    license_path = None
    scanned_pdfs = 0
    seen = set()
    for raw in entries:
        if not raw or any(ord(c) < 32 or ord(c) == 127 for c in raw):
            _reject("empty or control-bearing installed path")
        path = Path(raw)
        if not path.is_absolute():
            _reject("CMake install manifest must list absolute paths")
        normalized = raw.replace("\\", "/").casefold()
        if normalized in seen:
            _reject("duplicate installed path: " + raw)
        seen.add(normalized)
        if ("/tests/external_source_corpus/" in normalized or
                "/external_source_corpus/" in normalized or
                normalized.endswith("/" + SOURCE_FILENAME) or
                normalized.endswith("/texample-cc-by-sa-4.0-notice.txt")):
            _reject("test-only licensed original leaked into installed SDK: " + raw)

        if normalized.endswith("/quantapdf/third_party.md"):
            if notice_path is not None:
                _reject("duplicate SDK third-party notice")
            notice_path = path
        elif normalized.endswith("/quantapdf/license"):
            if license_path is not None:
                _reject("duplicate SDK project license")
            license_path = path

        # Check all installed files, regardless of suffix. Renaming the
        # unmodified PDF to .dat/.bin must not bypass the source exclusion.
        # Hash only files with the exact known size to keep this bounded.
        if normalized.endswith(".pdf"):
            if path.is_symlink() or not path.is_file():
                _reject("unreadable installed PDF: " + raw)
            if path.stat().st_size > MAX_PDF_BYTES:
                _reject("installed PDF exceeds audit budget: " + raw)
            scanned_pdfs += 1
        if path.is_file() and path.stat().st_size == SOURCE_BYTES:
            if _file_sha256(path) == SOURCE_SHA256:
                _reject("renamed original TeXample PDF installed in SDK: " + raw)

    if notice_path is None or license_path is None:
        _reject("SDK lacks installed THIRD_PARTY.md or LICENSE")
    if (notice_path.is_symlink() or not notice_path.is_file() or
            license_path.is_symlink() or not license_path.is_file()):
        _reject("installed legal notices absent or symlinked")
    if notice_path.stat().st_size > 256 * 1024:
        _reject("SDK third-party notice too large")
    try:
        notice = notice_path.read_text(encoding="utf-8")
    except UnicodeError as exc:
        _reject(f"SDK third-party notice is not valid UTF-8: {exc}")
    for phrase in REQUIRED_NOTICE:
        if phrase not in notice:
            _reject("installed license notice lacks attribution: " + phrase)
    return {
        "result": "test_only_pdf_excluded",
        "installed_entries": len(entries),
        "pdf_entries_checked": scanned_pdfs,
        "third_party_notice": "verified",
        "source_pdf_installed": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-manifest", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = check_install_manifest(args.install_manifest)
    except (OSError, SDKDataBoundaryError) as exc:
        print("SDK source license boundary rejected: " + str(exc), file=sys.stderr)
        return 2
    print("[sdk-data-boundary] " + result["result"] +
          f" entries={result['installed_entries']}" +
          f" pdfs_scanned={result['pdf_entries_checked']}" +
          " THIRD_PARTY=verified source_pdf_installed=false")
    return 0


if __name__ == "__main__":
    sys.exit(main())
