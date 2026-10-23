#!/usr/bin/env python3
"""Quarantined local PDF scan for Issue #262 W* candidates. Discovery only."""
from __future__ import annotations
import hashlib
import json
import re
import zlib
from pathlib import Path

ROOT = Path("tests/external_wstar_candidates")
WSTAR = re.compile(rb"(?<![A-Za-z0-9])W\\*(?=\\s|%|$)")
TEXT_OP = re.compile(rb"(?<![A-Za-z0-9])(?:BT|ET|Tj|TJ|Tf|Td|TD|Tm|Tr)(?![A-Za-z0-9])")
PATH_OP = re.compile(rb"(?<![A-Za-z0-9])(?:m|l|c|v|y|h|re)(?![A-Za-z0-9])")
PATH_END = re.compile(rb"(?<![A-Za-z0-9])(?:S|s|f|F|B|B\\*|b|b\\*|n)(?![A-Za-z0-9])")

def scan(data: bytes):
    page_refs = set()
    for match in re.finditer(rb"/Contents\\s*(\\[[^\\]]*\\]|\\d+\\s+\\d+\\s+R)", data):
        page_refs.update(int(value) for value in re.findall(rb"(\\d+)\\s+\\d+\\s+R", match.group(1)))
    direct_lengths = {
        int(match.group(1)): int(match.group(2))
        for match in re.finditer(rb"(?m)^\\s*(\\d+)\\s+\\d+\\s+obj\\s+(\\d+)\\s+endobj", data)
    }
    streams, page_text_ops = [], 0
    for obj in re.finditer(rb"(?m)^(\\d+)\\s+\\d+\\s+obj\\b", data):
        object_id = int(obj.group(1))
        body_start = obj.end()
        marker = re.search(rb"stream\\r?\\n", data[body_start:])
        if marker is None:
            continue
        marker_start = body_start + marker.start()
        if data.find(b"endobj", body_start, marker_start) >= 0:
            continue
        dictionary = data[body_start:marker_start]
        payload_start = body_start + marker.end()
        length_match = re.search(rb"/Length\\s+(\\d+)(?:\\s+\\d+\\s+R)?", dictionary)
        length = int(length_match.group(1)) if length_match else None
        if length_match and re.search(rb"/Length\\s+\\d+\\s+\\d+\\s+R", dictionary):
            length = direct_lengths.get(length)
        payload_end = payload_start + length if length is not None and payload_start + length <= len(data) else data.find(b"endstream", payload_start)
        if payload_end < payload_start:
            continue
        payload = data[payload_start:payload_end]
        if payload.endswith(b"\\r\\n"):
            payload = payload[:-2]
        elif payload.endswith(b"\\n"):
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
            position = hit.start()
            previous_path_ends = [m.end() for m in PATH_END.finditer(decoded[:position])]
            segment_start = previous_path_ends[-1] if previous_path_ends else max(0, position - 500)
            segment = decoded[segment_start:position]
            tokens = [m.group().decode("ascii") for m in PATH_OP.finditer(segment)]
            rectangles, moves = tokens.count("re"), tokens.count("m")
            clips.append({
                "rectangles_heuristic": rectangles,
                "move_ops_heuristic": moves,
                "nonrect_path_ops_heuristic": [token for token in tokens if token != "re"],
                "context": decoded[max(0, position - 100):position + 25].decode("latin-1", errors="replace"),
            })
        streams.append({
            "object": object_id,
            "page_content_reference": object_id in page_refs,
            "wstar_count": len(hits),
            "stream_text_operator_count": text_count,
            "clips": clips[:12],
        })
    page_wstar = sum(stream["wstar_count"] for stream in streams if stream["page_content_reference"])
    return page_wstar, streams, page_text_ops

def main():
    results = []
    for path in sorted(ROOT.glob("*.pdf")):
        data = path.read_bytes()
        page_wstar, streams, page_text = scan(data)
        clips = [clip for stream in streams if stream["page_content_reference"] for clip in stream["clips"]]
        meaningful = [clip for clip in clips if clip["nonrect_path_ops_heuristic"] or clip["rectangles_heuristic"] + clip["move_ops_heuristic"] >= 2]
        results.append({
            "path": str(path),
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "page_content_wstar_count": page_wstar,
            "page_text_operator_count": page_text,
            "vector_only_page_heuristic": page_text == 0,
            "page_clip_count": len(clips),
            "parity_discriminating_clip_heuristic_count": len(meaningful),
            "streams": streams,
        })
    print(json.dumps({
        "status": "discovery only; these candidates are not in the formal corpus",
        "files_scanned": len(results),
        "results": results,
    }, indent=2, ensure_ascii=False))

if __name__ == "__main__":
    main()
