#!/usr/bin/env python3
"""Discovery-only scan of explicitly licensed/public-domain Commons original PDFs."""
from __future__ import annotations
import hashlib
import json
import re
import time
import urllib.request
import zlib

CANDIDATES = [
    {
        "file": "Amsterdam Museum logo.pdf",
        "url": "https://upload.wikimedia.org/wikipedia/commons/6/6c/Amsterdam_Museum_logo.pdf",
        "page": "https://commons.wikimedia.org/wiki/File:Amsterdam_Museum_logo.pdf",
        "license": "Commons public domain (PD-textlogo / simple shapes); trademark caveat",
        "producer": "Adobe Illustrator CS4 / Adobe PDF library; Commons marks this as BadPDF/superseded",
    },
]
MAX_BYTES = 1_000_000
WSTAR = re.compile(rb"(?<![A-Za-z0-9])W\*(?=\s|%|$)")
TEXT_OP = re.compile(rb"(?<![A-Za-z0-9])(?:BT|ET|Tj|TJ|Tf|Td|TD|Tm|Tr)(?![A-Za-z0-9])")
PATH_OP = re.compile(rb"(?<![A-Za-z0-9])(?:m|l|c|v|y|h|re)(?![A-Za-z0-9])")
PATH_END = re.compile(rb"(?<![A-Za-z0-9])(?:S|s|f|F|B|B\*|b|b\*|n)(?![A-Za-z0-9])")

def scan_pdf(data):
    page_refs = set()
    for match in re.finditer(rb"/Contents\s*(\[[^\]]*\]|\d+\s+\d+\s+R)", data):
        page_refs.update(int(value) for value in re.findall(rb"(\d+)\s+\d+\s+R", match.group(1)))
    direct_lengths = {
        int(match.group(1)): int(match.group(2))
        for match in re.finditer(rb"(?m)^\s*(\d+)\s+\d+\s+obj\s+(\d+)\s+endobj", data)
    }
    streams, page_text_ops = [], 0
    for obj in re.finditer(rb"(?m)^(\d+)\s+\d+\s+obj\b", data):
        object_id = int(obj.group(1))
        body_start = obj.end()
        marker = re.search(rb"stream\r?\n", data[body_start:])
        if marker is None:
            continue
        marker_start = body_start + marker.start()
        if data.find(b"endobj", body_start, marker_start) >= 0:
            continue
        dictionary = data[body_start:marker_start]
        payload_start = body_start + marker.end()
        lm = re.search(rb"/Length\s+(\d+)(?:\s+\d+\s+R)?", dictionary)
        length = int(lm.group(1)) if lm else None
        if lm and re.search(rb"/Length\s+\d+\s+\d+\s+R", dictionary):
            length = direct_lengths.get(length)
        payload_end = payload_start + length if length is not None and payload_start + length <= len(data) else data.find(b"endstream", payload_start)
        if payload_end < payload_start:
            continue
        payload = data[payload_start:payload_end]
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
        hits = list(WSTAR.finditer(decoded))
        if not hits:
            continue
        clips = []
        for hit in hits:
            pos = hit.start()
            previous_ends = [m.end() for m in PATH_END.finditer(decoded[:pos])]
            segment_start = previous_ends[-1] if previous_ends else max(0, pos - 500)
            tokens = [m.group().decode("ascii") for m in PATH_OP.finditer(decoded[segment_start:pos])]
            rects, moves = tokens.count("re"), tokens.count("m")
            clips.append({
                "rectangles_heuristic": rects,
                "move_ops_heuristic": moves,
                "nonrect_path_ops_heuristic": [t for t in tokens if t != "re"],
                "context": decoded[max(0, pos - 100):pos + 25].decode("latin-1", errors="replace"),
            })
        streams.append({"object": object_id, "page_content_reference_heuristic": object_id in page_refs, "wstar_count": len(hits), "text_operator_count": text_count, "clips": clips[:12]})
    page_wstar = sum(s["wstar_count"] for s in streams if s["page_content_reference_heuristic"])
    page_clips = [clip for s in streams if s["page_content_reference_heuristic"] for clip in s["clips"]]
    meaningful = [clip for clip in page_clips if clip["nonrect_path_ops_heuristic"] or clip["rectangles_heuristic"] + clip["move_ops_heuristic"] >= 2]
    return page_wstar, page_text_ops, len(page_clips), len(meaningful), streams

def main():
    reports = []
    for item in CANDIDATES:
        result = {key: item[key] for key in ("file", "page", "license", "producer")}
        last_error = None
        data = b""
        for attempt in range(4):
            try:
                request = urllib.request.Request(item["url"], headers={"User-Agent": "QuantaPDF-Issue262-Wstar-Research/1.0"})
                with urllib.request.urlopen(request, timeout=30) as response:
                    data = response.read(MAX_BYTES + 1)
                last_error = None
                break
            except Exception as exc:
                last_error = exc
                if attempt < 3:
                    time.sleep(5 * (attempt + 1))
        if last_error is not None:
            reports.append({**result, "status": "download-error", "error": repr(last_error)})
            continue
        if len(data) > MAX_BYTES or not data.startswith(b"%PDF-"):
            reports.append({**result, "status": "invalid-or-over-size-limit", "bytes": len(data)})
            continue
        page_wstar, page_text, clips, meaningful, streams = scan_pdf(data)
        reports.append({
            **result,
            "status": "scanned-discovery-only",
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "page_content_wstar_count": page_wstar,
            "page_text_operator_count_heuristic": page_text,
            "page_clip_count_heuristic": clips,
            "parity_discriminating_clip_count_heuristic": meaningful,
            "vector_only_page_heuristic": page_text == 0,
            "streams": streams,
        })
        time.sleep(3)
    print(json.dumps({
        "purpose": "Inspect source/rights-documented original PDF logos only as discovery candidates; no file is admitted.",
        "files_checked": len(reports),
        "wstar_positive_count": sum(1 for item in reports if item.get("page_content_wstar_count", 0) > 0),
        "results": reports,
        "status": "discovery only; formal corpus and counters unchanged",
    }, indent=2, ensure_ascii=False))

if __name__ == "__main__":
    main()
