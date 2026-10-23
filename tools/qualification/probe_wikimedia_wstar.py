#!/usr/bin/env python3
"""Discovery-only probe of license-labelled Wikimedia Commons PDFs for W* clips."""
from __future__ import annotations
import hashlib
import json
import re
import urllib.parse
import urllib.request
import zlib
import time

CANDIDATES = [
    {"file": "Phc-logo.pdf", "page": "https://commons.wikimedia.org/wiki/File:Phc-logo.pdf", "license": "CC BY-SA 4.0"},
    {"file": "EFF-logo-plain.pdf", "page": "https://commons.wikimedia.org/wiki/File:EFF-logo-plain.pdf", "license": "CC BY 3.0 US"},
]
MAX_BYTES = 8_000_000
WSTAR = re.compile(rb"(?<![A-Za-z0-9])W\*(?=\s|%|$)")
TEXT_OP = re.compile(rb"(?<![A-Za-z0-9])(?:BT|ET|Tj|TJ|Tf|Td|TD|Tm|Tr)(?![A-Za-z0-9])")
PATH_OP = re.compile(rb"(?<![A-Za-z0-9])(?:m|l|c|v|y|h|re)(?![A-Za-z0-9])")
PATH_END = re.compile(rb"(?<![A-Za-z0-9])(?:S|s|f|F|B|B\*|b|b\*|n)(?![A-Za-z0-9])")

def scan_pdf(data: bytes):
    page_refs = set()
    for m in re.finditer(rb"/Contents\s*(\[[^\]]*\]|\d+\s+\d+\s+R)", data):
        page_refs.update(int(x) for x in re.findall(rb"(\d+)\s+\d+\s+R", m.group(1)))
    length_objects = {
        int(m.group(1)): int(m.group(2))
        for m in re.finditer(rb"(?m)^\s*(\d+)\s+\d+\s+obj\s+(\d+)\s+endobj", data)
    }
    stream_reports = []
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
        if not hits:
            continue
        clips = []
        for p in hits:
            previous_ends = [m.end() for m in PATH_END.finditer(decoded[:p])]
            path_start = previous_ends[-1] if previous_ends else max(0, p - 500)
            path_bytes = decoded[path_start:p]
            path_tokens = [m.group().decode("ascii") for m in PATH_OP.finditer(path_bytes)]
            rect_count = path_tokens.count("re")
            move_count = path_tokens.count("m")
            subpaths = rect_count + move_count
            clips.append({
                "subpath_count_approx": subpaths,
                "rect_count": rect_count,
                "nonrect_path_ops": [t for t in path_tokens if t != "re"],
                "context": decoded[max(0, p - 120):p + 30].decode("latin-1", errors="replace"),
            })
        stream_reports.append({
            "object": object_id,
            "page_content": object_id in page_refs,
            "wstar_count": len(hits),
            "text_operator_count": text_count,
            "clips": clips[:12],
        })
    return stream_reports, page_text_ops

def main():
    results = []
    for item in CANDIDATES:
        url = "https://commons.wikimedia.org/wiki/Special:Redirect/file/" + urllib.parse.quote(item["file"])
        req = urllib.request.Request(url, headers={"User-Agent": "QuantaPDF-Wstar-Probe/1.0 (conformance research)"})
        try:
            last_error = None
            for attempt in range(4):
                try:
                    with urllib.request.urlopen(req, timeout=45) as response:
                        data = response.read(MAX_BYTES + 1)
                        final_url = response.geturl()
                    last_error = None
                    break
                except Exception as exc:
                    last_error = exc
                    if attempt < 3:
                        time.sleep(15 * (attempt + 1))
            if last_error is not None:
                raise last_error
            if len(data) > MAX_BYTES:
                results.append({**item, "status": "skipped-over-size-limit", "download_url": final_url})
                continue
            if not data.startswith(b"%PDF-"):
                results.append({**item, "status": "not-pdf-response", "download_url": final_url, "bytes": len(data)})
                continue
            streams, page_text_ops = scan_pdf(data)
            page_wstar = sum(s["wstar_count"] for s in streams if s["page_content"])
            results.append({
                **item,
                "status": "scanned-discovery-only",
                "download_url": final_url,
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "page_content_wstar_count": page_wstar,
                "page_content_text_operator_count": page_text_ops,
                "vector_only_page_content_heuristic": page_text_ops == 0,
                "streams": streams,
            })
        except Exception as exc:
            results.append({**item, "status": "download-or-scan-error", "error": repr(exc)})
    print(json.dumps({
        "purpose": "Find license-labelled original PDFs that actually use W*; no file is admitted by this probe.",
        "results": results,
    }, indent=2, ensure_ascii=False))

if __name__ == "__main__":
    main()
