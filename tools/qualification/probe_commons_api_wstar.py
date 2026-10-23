#!/usr/bin/env python3
"""Discovery-only Wikimedia Commons scan for rights-described original PDFs using W*."""
from __future__ import annotations
import hashlib
import json
import re
import time
import urllib.parse
import urllib.request
import zlib

API = "https://commons.wikimedia.org/w/api.php"
USER_AGENT = "QuantaPDF-Issue262-Wstar-Research/1.0 (source qualification; contact via repository issue)"
MAX_PDF_BYTES = 2_000_000
MAX_DOWNLOADS = 70
WSTAR = re.compile(rb"(?<![A-Za-z0-9])W\\*(?=\\s|%|$)")
TEXT_OP = re.compile(rb"(?<![A-Za-z0-9])(?:BT|ET|Tj|TJ|Tf|Td|TD|Tm|Tr)(?![A-Za-z0-9])")
PATH_OP = re.compile(rb"(?<![A-Za-z0-9])(?:m|l|c|v|y|h|re)(?![A-Za-z0-9])")
PATH_END = re.compile(rb"(?<![A-Za-z0-9])(?:S|s|f|F|B|B\\*|b|b\\*|n)(?![A-Za-z0-9])")

def get_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as response:
        return json.load(response)

def api(params):
    return get_json(API + "?" + urllib.parse.urlencode(params))

def text_meta(imageinfo, key):
    value = imageinfo.get("extmetadata", {}).get(key, {}).get("value", "")
    return re.sub(r"<[^>]+>", " ", value).strip()

def scan_pdf(data):
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
            ends = [m.end() for m in PATH_END.finditer(decoded[:position])]
            segment_start = ends[-1] if ends else max(0, position - 500)
            tokens = [m.group().decode("ascii") for m in PATH_OP.finditer(decoded[segment_start:position])]
            rectangles, moves = tokens.count("re"), tokens.count("m")
            clips.append({
                "rectangles_heuristic": rectangles,
                "move_ops_heuristic": moves,
                "nonrect_path_ops_heuristic": [token for token in tokens if token != "re"],
                "context": decoded[max(0, position - 80):position + 25].decode("latin-1", errors="replace"),
            })
        streams.append({
            "object": object_id,
            "page_content_reference_heuristic": object_id in page_refs,
            "wstar_count": len(hits),
            "stream_text_operator_count": text_count,
            "clips": clips[:6],
        })
    page_wstar = sum(s["wstar_count"] for s in streams if s["page_content_reference_heuristic"])
    page_clips = [clip for s in streams if s["page_content_reference_heuristic"] for clip in s["clips"]]
    meaningful = [clip for clip in page_clips if clip["nonrect_path_ops_heuristic"] or clip["rectangles_heuristic"] + clip["move_ops_heuristic"] >= 2]
    return page_wstar, page_text_ops, len(page_clips), len(meaningful), streams

def main():
    searches = [
        'filemime:application/pdf incategory:"CC-Zero"',
        'filemime:application/pdf incategory:"Public domain"',
    ]
    pages_by_title = {}
    search_errors = []
    for query in searches:
        try:
            result = api({
                "action": "query", "generator": "search", "gsrsearch": query,
                "gsrnamespace": 6, "gsrlimit": 50, "prop": "imageinfo",
                "iiprop": "url|size|sha1|mime|extmetadata", "format": "json",
            })
            for page in result.get("query", {}).get("pages", {}).values():
                pages_by_title[page.get("title", "")] = page
        except Exception as exc:
            search_errors.append({"query": query, "error": repr(exc)})
        time.sleep(1)
    reports, scanned, skipped = [], 0, 0
    for title, page in list(pages_by_title.items())[:MAX_DOWNLOADS]:
        infos = page.get("imageinfo", [])
        if not infos:
            continue
        info = infos[0]
        if info.get("mime") != "application/pdf":
            continue
        size = int(info.get("size", 0))
        if size <= 0 or size > MAX_PDF_BYTES:
            skipped += 1
            continue
        license_name = text_meta(info, "LicenseShortName")
        license_url = text_meta(info, "LicenseUrl")
        artist = text_meta(info, "Artist")
        page_url = page.get("canonicalurl") or ("https://commons.wikimedia.org/wiki/" + urllib.parse.quote(title.replace(" ", "_")))
        try:
            request = urllib.request.Request(info["url"], headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=30) as response:
                data = response.read(MAX_PDF_BYTES + 1)
            if len(data) > MAX_PDF_BYTES or not data.startswith(b"%PDF-"):
                reports.append({"title": title, "page_url": page_url, "status": "invalid-or-over-limit-download", "bytes": len(data)})
                continue
            scanned += 1
            page_wstar, page_text, clip_count, meaningful_count, streams = scan_pdf(data)
            if page_wstar:
                reports.append({
                    "title": title,
                    "page_url": page_url,
                    "original_url": info["url"],
                    "license_short_name": license_name,
                    "license_url": license_url,
                    "artist": artist,
                    "api_sha1": info.get("sha1"),
                    "bytes": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "page_content_wstar_count": page_wstar,
                    "page_text_operator_count_heuristic": page_text,
                    "page_clip_count_heuristic": clip_count,
                    "parity_discriminating_clip_count_heuristic": meaningful_count,
                    "vector_only_page_heuristic": page_text == 0,
                    "streams": streams,
                    "status": "discovery-only; verify source rights and render pixels before admission",
                })
        except Exception as exc:
            reports.append({"title": title, "page_url": page_url, "license_short_name": license_name, "status": "download-or-scan-error", "error": repr(exc)})
        time.sleep(0.25)
    print(json.dumps({
        "purpose": "Find genuinely original, license-described Commons PDFs that use W* in page content; this scan does not admit files.",
        "searches": searches,
        "search_errors": search_errors,
        "unique_pdf_page_results": len(pages_by_title),
        "pdfs_scanned": scanned,
        "pdfs_skipped_over_2mb": skipped,
        "wstar_candidate_count": len(reports),
        "candidates": reports,
        "status": "discovery only; formal corpus and counters unchanged",
    }, indent=2, ensure_ascii=False))

if __name__ == "__main__":
    main()
