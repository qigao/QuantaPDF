#!/usr/bin/env python3
"""Discovery-only scan of pinned Ghostscript/GhostPDL repository PDFs for W* clips."""
from __future__ import annotations
import hashlib
import io
import json
import re
import tarfile
import urllib.request
import zlib

REPO = "ArtifexSoftware/ghostpdl"
COMMIT = "b0ef31a9de66f732df0f6dd8b42f9340a14874e4"
ARCHIVE_URL = f"https://github.com/{REPO}/archive/{COMMIT}.tar.gz"
MAX_PDF_BYTES = 250_000
WSTAR = re.compile(rb"(?<![A-Za-z0-9])W\*(?=\s|%|$)")
TEXT_OP = re.compile(rb"(?<![A-Za-z0-9])(?:BT|ET|Tj|TJ|Tf|Td|TD|Tm|Tr)(?![A-Za-z0-9])")
PATH_OP = re.compile(rb"(?<![A-Za-z0-9])(?:m|l|c|v|y|h|re)(?![A-Za-z0-9])")
PATH_END = re.compile(rb"(?<![A-Za-z0-9])(?:S|s|f|F|B|B\*|b|b\*|n)(?![A-Za-z0-9])")

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
        lm = re.search(rb"/Length\s+(\d+)(?:\s+\d+\s+R)?", dictionary)
        length = int(lm.group(1)) if lm else None
        if lm and re.search(rb"/Length\s+\d+\s+\d+\s+R", dictionary):
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
        if not hits:
            continue
        clips = []
        for p in hits:
            ends = [m.end() for m in PATH_END.finditer(decoded[:p])]
            path_start = ends[-1] if ends else max(0, p - 500)
            tokens = [m.group().decode("ascii") for m in PATH_OP.finditer(decoded[path_start:p])]
            rects = tokens.count("re")
            moves = tokens.count("m")
            meaningful = bool([t for t in tokens if t != "re"]) or rects + moves >= 2
            clips.append({
                "subpaths_approx": rects + moves,
                "rectangles": rects,
                "nonrect_path_ops": [t for t in tokens if t != "re"],
                "parity_discriminating_geometry_heuristic": meaningful,
                "context": decoded[max(0, p - 75):p + 25].decode("latin-1", errors="replace"),
            })
        streams.append({
            "object": object_id,
            "page_content": object_id in page_refs,
            "wstar_count": len(hits),
            "text_operator_count": text_count,
            "clips": clips[:8],
        })
    return streams, page_text_ops

def main():
    req = urllib.request.Request(ARCHIVE_URL, headers={"User-Agent": "QuantaPDF-Wstar-Probe/1.0"})
    with urllib.request.urlopen(req, timeout=120) as response:
        archive_bytes = response.read()
    candidates = []
    scanned = skipped = 0
    with tarfile.open(fileobj=io.BytesIO(archive_bytes), mode="r:gz") as archive:
        for member in archive.getmembers():
            if not member.isfile() or not member.name.lower().endswith(".pdf"):
                continue
            if member.size > MAX_PDF_BYTES:
                skipped += 1
                continue
            handle = archive.extractfile(member)
            if handle is None:
                continue
            data = handle.read()
            scanned += 1
            streams, text_ops = scan(data)
            page_clips = [clip for stream in streams if stream["page_content"] for clip in stream["clips"]]
            page_wstar = sum(stream["wstar_count"] for stream in streams if stream["page_content"])
            meaningful = [clip for clip in page_clips if clip["parity_discriminating_geometry_heuristic"]]
            if page_wstar:
                candidates.append({
                    "path": member.name.split("/", 1)[-1],
                    "bytes": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "git_blob_sha": hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest(),
                    "page_content_wstar_count": page_wstar,
                    "page_content_text_operator_count": text_ops,
                    "vector_only_page_heuristic": text_ops == 0,
                    "parity_discriminating_clip_count_heuristic": len(meaningful),
                    "streams": streams,
                })
    candidates.sort(key=lambda x: (x["parity_discriminating_clip_count_heuristic"] == 0, not x["vector_only_page_heuristic"], -x["parity_discriminating_clip_count_heuristic"], x["path"]))
    print(json.dumps({
        "repository": REPO,
        "pinned_commit": COMMIT,
        "repository_license": "Apache-2.0; candidate PDF provenance still requires review",
        "pdfs_scanned": scanned,
        "pdfs_skipped_over_300kb": skipped,
        "candidate_count": len(candidates),
        "candidates": candidates[:30],
        "status": "discovery only; not formal corpus evidence",
    }, indent=2, ensure_ascii=False))

if __name__ == "__main__":
    main()
