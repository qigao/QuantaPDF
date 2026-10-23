#!/usr/bin/env python3
"""Quarantined PDFKit corpus probe for actual even-odd clipping operators.

Discovery only: this script does not admit PDFs or make per-file license claims.
"""
from __future__ import annotations
import hashlib
import io
import json
import re
import tarfile
import urllib.request
import zlib

REPO = "foliojs/pdfkit"
COMMIT = "9de53ec021b6a7b959e21be189660ebb30f41f0c"
ARCHIVE_URL = f"https://github.com/{REPO}/archive/{COMMIT}.tar.gz"
MAX_PDF_BYTES = 2_000_000
WSTAR = re.compile(rb"(?<![A-Za-z0-9])W\*(?=\s|%|$)")
TEXT_OP = re.compile(rb"(?<![A-Za-z0-9])(?:BT|ET|Tj|TJ|Tf|Td|TD|Tm|Tr)(?![A-Za-z0-9])")

def scan(data: bytes):
    page_refs = set()
    for m in re.finditer(rb"/Contents\s*(\[[^\]]*\]|\d+\s+\d+\s+R)", data):
        page_refs.update(int(x) for x in re.findall(rb"(\d+)\s+\d+\s+R", m.group(1)))
    length_objects = {
        int(m.group(1)): int(m.group(2))
        for m in re.finditer(rb"(?m)^\s*(\d+)\s+\d+\s+obj\s+(\d+)\s+endobj", data)
    }
    streams = []
    page_text_ops = 0
    for obj in re.finditer(rb"(?m)^(\d+)\s+\d+\s+obj\b", data):
        object_id = int(obj.group(1))
        body_start = obj.end()
        marker = re.search(rb"stream\r?\n", data[body_start:])
        if marker is None:
            continue
        stream_marker = body_start + marker.start()
        if data.find(b"endobj", body_start, stream_marker) >= 0:
            continue
        dictionary = data[body_start:stream_marker]
        start = body_start + marker.end()
        length_match = re.search(rb"/Length\s+(\d+)(?:\s+\d+\s+R)?", dictionary)
        length = None
        if length_match:
            length = int(length_match.group(1))
            if re.search(rb"/Length\s+\d+\s+\d+\s+R", dictionary):
                length = length_objects.get(length)
        end = start + length if length is not None and start + length <= len(data) else data.find(b"endstream", start)
        if end < start:
            continue
        payload = data[start:end]
        if payload.endswith(b"\r\n"):
            payload = payload[:-2]
        elif payload.endswith(b"\n"):
            payload = payload[:-1]
        decoded = payload
        if b"/FlateDecode" in dictionary:
            try:
                decoded = zlib.decompress(payload)
            except zlib.error:
                continue
        text_count = len(list(TEXT_OP.finditer(decoded)))
        if object_id in page_refs:
            page_text_ops += text_count
        hits = [m.start() for m in WSTAR.finditer(decoded)]
        if hits:
            streams.append({
                "object": object_id,
                "page_content": object_id in page_refs,
                "wstar_count": len(hits),
                "text_operator_count": text_count,
                "contexts": [
                    decoded[max(0, p - 100):p + 120].decode("latin-1", errors="replace")
                    for p in hits[:8]
                ],
            })
    return streams, page_text_ops

def main():
    req = urllib.request.Request(ARCHIVE_URL, headers={"User-Agent": "QuantaPDF-Wstar-Probe/1.0"})
    with urllib.request.urlopen(req, timeout=90) as response:
        archive_bytes = response.read()
    candidates = []
    scanned = skipped = 0
    with tarfile.open(fileobj=io.BytesIO(archive_bytes), mode="r:gz") as archive:
        for member in archive.getmembers():
            if not member.isfile() or not member.name.lower().endswith(".pdf"):
                continue
            if not ("/examples/" in member.name or "/tests/" in member.name):
                continue
            if member.size > MAX_PDF_BYTES:
                skipped += 1
                continue
            handle = archive.extractfile(member)
            if handle is None:
                continue
            data = handle.read()
            scanned += 1
            streams, page_text_ops = scan(data)
            page_wstar = sum(s["wstar_count"] for s in streams if s["page_content"])
            if page_wstar:
                candidates.append({
                    "path": member.name.split(f"{COMMIT}/", 1)[-1],
                    "bytes": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "git_blob_sha": hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest(),
                    "page_content_wstar_count": page_wstar,
                    "page_content_text_operator_count": page_text_ops,
                    "vector_only_page_content": page_text_ops == 0,
                    "streams": streams,
                })
    print(json.dumps({
        "repository": REPO,
        "pinned_commit": COMMIT,
        "repo_license": "MIT (repository LICENSE; individual embedded content still requires review)",
        "pdfs_scanned": scanned,
        "pdfs_skipped_over_2mb": skipped,
        "candidate_count": len(candidates),
        "candidates": sorted(candidates, key=lambda x: (not x["vector_only_page_content"], -x["page_content_wstar_count"], x["path"])),
        "status": "discovery only; not formal corpus evidence",
    }, indent=2, ensure_ascii=False))

if __name__ == "__main__":
    main()
