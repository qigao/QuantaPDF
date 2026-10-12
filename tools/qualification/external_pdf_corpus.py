#!/usr/bin/env python3
"""Offline, fail-closed admission for independently sourced PDF corpus files.

This tool verifies a *declared* provenance and license-review record plus the
exact bytes' SHA-256. It does not grant any license, download remote content,
run the PDF renderer, or claim native PDF compatibility.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from urllib.parse import urlsplit

MAX_MANIFEST_BYTES = 256 * 1024
MAX_DOCUMENT_BYTES = 16 * 1024 * 1024
MAX_DOCUMENTS = 64
ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]{2,79}$")
TAG_RE = re.compile(r"^[a-z0-9][a-z0-9_.+-]{1,63}$")
HASH_RE = re.compile(r"^[a-f0-9]{64}$")
SPDX_ALLOWLIST = frozenset({
    "CC0-1.0", "CC-BY-4.0", "Apache-2.0", "BSD-3-Clause", "MIT"
})
EXPECTED_NATIVE = frozenset({
    "supported-path-clip", "supported-blank-page", "unsupported", "malformed"
})
ROOT_FIELDS = frozenset({"schema_version", "corpus_id", "documents"})
DOC_FIELDS = frozenset({
    "id", "source_url", "source_revision", "sha256", "size_bytes",
    "license_spdx", "license_url", "license_review_url", "attribution",
    "feature_tags", "page_index", "expected_native_outcome"
})


class CorpusAdmissionError(ValueError):
    """An untrusted external corpus input fails the admission contract."""


def _reject(message: str) -> None:
    raise CorpusAdmissionError(message)


def _unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            _reject("duplicate JSON key: " + key)
        result[key] = value
    return result


def _required_string(record: dict, key: str, max_size: int = 512) -> str:
    value = record.get(key)
    if not isinstance(value, str) or not value or len(value) > max_size:
        _reject(f"missing or invalid {key}")
    if any(ord(ch) < 32 or ord(ch) == 127 for ch in value):
        _reject(f"control character in {key}")
    return value


def _https(record: dict, key: str) -> str:
    value = _required_string(record, key, 1024)
    try:
        parsed = urlsplit(value)
        valid = (
            parsed.scheme == "https" and parsed.hostname is not None
            and parsed.port in (None, 443) and parsed.username is None
            and parsed.password is None and parsed.fragment == ""
        )
    except ValueError:
        valid = False
    if not valid:
        _reject(f"{key} must be an uncredentialed HTTPS URL")
    return value


def _load_manifest(path: Path) -> dict:
    if not path.is_file() or path.is_symlink():
        _reject("manifest is missing or is a symlink")
    if path.stat().st_size > MAX_MANIFEST_BYTES:
        _reject("manifest exceeds its byte budget")
    try:
        manifest = json.loads(path.read_bytes().decode("utf-8"),
                              object_pairs_hook=_unique_object)
    except (UnicodeError, json.JSONDecodeError) as exc:
        _reject(f"malformed UTF-8 JSON manifest: {exc}")
    if not isinstance(manifest, dict) or set(manifest) != ROOT_FIELDS:
        _reject("manifest root fields must match schema v1 exactly")
    if type(manifest["schema_version"]) is not int or manifest["schema_version"] != 1:
        _reject("unsupported manifest schema version")
    if manifest["corpus_id"] != "external-source-pdf":
        _reject("wrong external corpus identity")
    if not isinstance(manifest["documents"], list) or len(manifest["documents"]) > MAX_DOCUMENTS:
        _reject("invalid external document list or count")
    return manifest


def _validate_metadata(raw: object) -> dict:
    if not isinstance(raw, dict) or set(raw) != DOC_FIELDS:
        _reject("document metadata is missing or has unknown fields")
    document_id = _required_string(raw, "id", 80)
    if not ID_RE.fullmatch(document_id) or document_id in (".", ".."):
        _reject("invalid external document ID")
    source_url = _https(raw, "source_url")
    source_revision = _required_string(raw, "source_revision", 160)
    checksum = _required_string(raw, "sha256", 64)
    if not HASH_RE.fullmatch(checksum):
        _reject("SHA-256 must be 64 lowercase hex characters")
    size_bytes = raw["size_bytes"]
    if type(size_bytes) is not int or not 1 <= size_bytes <= MAX_DOCUMENT_BYTES:
        _reject("invalid external PDF byte budget")
    license_spdx = _required_string(raw, "license_spdx", 64)
    if license_spdx not in SPDX_ALLOWLIST:
        _reject("unreviewed or unsupported SPDX license identifier")
    _https(raw, "license_url")
    review_url = _https(raw, "license_review_url")
    if not review_url.startswith(("https://github.com/qigao/QuantaPDF/issues/",
                                  "https://github.com/qigao/QuantaPDF/pull/")):
        _reject("license review must link to a QuantaPDF issue or PR")
    _required_string(raw, "attribution", 1024)
    tags = raw["feature_tags"]
    if (not isinstance(tags, list) or not 1 <= len(tags) <= 32
            or any(not isinstance(tag, str) or not TAG_RE.fullmatch(tag) for tag in tags)
            or len(tags) != len(set(tags))):
        _reject("invalid or repeated feature tags")
    page_index = raw["page_index"]
    if type(page_index) is not int or not 0 <= page_index <= 255:
        _reject("invalid selected page index")
    expected = _required_string(raw, "expected_native_outcome", 40)
    if expected not in EXPECTED_NATIVE:
        _reject("unknown expected native outcome")
    return {
        "id": document_id,
        "sha256": checksum,
        "size_bytes": size_bytes,
        "feature_tags": tags,
        "expected_native_outcome": expected,
        "license_spdx": license_spdx,
        "license_review_url": review_url,
        "source_url": source_url,
        "source_revision": source_revision,
        "page_index": page_index
    }


def _verify_pdf_file(path: Path, expected_hash: str, expected_size: int) -> None:
    if path.is_symlink() or not path.is_file():
        _reject("external PDF is absent or a symlink: " + path.name)
    if path.stat().st_size != expected_size:
        _reject("external PDF byte length mismatch: " + path.name)
    digest = hashlib.sha256()
    size = 0
    first = b""
    tail = b""
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(65536)
            if not chunk:
                break
            if size == 0:
                first = chunk[:5]
            size += len(chunk)
            if size > MAX_DOCUMENT_BYTES:
                _reject("external PDF exceeds byte budget")
            digest.update(chunk)
            tail = (tail + chunk)[-2048:]
    if first != b"%PDF-" or b"%%EOF" not in tail:
        _reject("file fails minimal PDF header/trailer checks: " + path.name)
    if size != expected_size or digest.hexdigest() != expected_hash:
        _reject("external PDF SHA-256 mismatch: " + path.name)


def verify_corpus(manifest_path: Path, assets_root: Path) -> dict:
    manifest = _load_manifest(manifest_path)
    if assets_root.is_symlink():
        _reject("external corpus directory cannot be a symlink")
    declared_ids = set()
    records = []
    for raw in manifest["documents"]:
        record = _validate_metadata(raw)
        if record["id"] in declared_ids:
            _reject("duplicate external PDF ID: " + record["id"])
        declared_ids.add(record["id"])
        _verify_pdf_file(assets_root / (record["id"] + ".pdf"),
                         record["sha256"], record["size_bytes"])
        records.append({
            "id": record["id"],
            "sha256": record["sha256"],
            "size_bytes": record["size_bytes"],
            "feature_tags": record["feature_tags"],
            "license_spdx_declared": record["license_spdx"],
            "license_review_url": record["license_review_url"],
            "page_index": record["page_index"],
            "expected_native_outcome": record["expected_native_outcome"],
            "integrity": "verified",
            "native_conformance": "not_run"
        })
    if assets_root.exists():
        if not assets_root.is_dir():
            _reject("external corpus assets path is not a directory")
        for file in assets_root.iterdir():
            if file.is_symlink() or file.is_file() and file.suffix.lower() == ".pdf":
                if file.name not in {id_ + ".pdf" for id_ in declared_ids}:
                    _reject("unregistered external file: " + file.name)
    return {
        "schema_version": 1,
        "corpus_id": "external-source-pdf",
        "fixture_kind": "external",
        "result": "integrity_verified_only" if records else "no_external_documents",
        "document_count": len(records),
        "native_conformance_passed_count": 0,
        "documents": records
    }


def atomic_report(path: Path, report: dict) -> None:
    if path.is_symlink():
        _reject("report destination cannot be a symlink")
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = Path(str(path) + ".tmp")
    if pending.is_symlink():
        _reject("temporary report cannot be a symlink")
    with pending.open("w", encoding="utf-8", newline="\n") as output:
        json.dump(report, output, sort_keys=True, indent=2, ensure_ascii=True)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())
    os.replace(pending, path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    # A failed invocation must never leave a previously successful report.
    for candidate in (args.report, Path(str(args.report) + ".tmp")):
        if candidate.is_symlink():
            print("error: report path is a symlink", file=sys.stderr)
            return 2
        try:
            candidate.unlink(missing_ok=True)
        except OSError as exc:
            print(f"error: cannot clear old report: {exc}", file=sys.stderr)
            return 2
    try:
        report = verify_corpus(args.manifest, args.assets)
        atomic_report(args.report, report)
    except (CorpusAdmissionError, OSError) as exc:
        print(f"external corpus admission rejected: {exc}", file=sys.stderr)
        return 2
    print(f"[external-corpus] {report['result']} documents={report['document_count']} "
          f"native_conformance_passed={report['native_conformance_passed_count']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
