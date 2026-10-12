#!/usr/bin/env python3
"""Bounded, offline *screening only* of original PDFs for W* path clips.

Not a PDF parser, provenance review, license grant, native rendering test, or
PDFium oracle. No result from this tool can increment conformance counters.
"""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import sys
import zlib

MAX_PDFS = 128
MAX_INPUT = 16 * 1024 * 1024
MAX_STREAMS = 64
MAX_DECODED_STREAM = 1024 * 1024
STREAM_MARKER = re.compile(rb"(?<![A-Za-z])stream(?:\r\n|\n|\r)")
OBJECT_HEADER = re.compile(rb"(?:^|[\r\n])\s*\d+\s+\d+\s+obj\b")
DIRECT_LENGTH = re.compile(rb"/Length\s+(\d+)(?:\s+(\d+)\s+R)?\b")
FILTER = re.compile(rb"/Filter\s*(\[[^\]]*\]|/[A-Za-z0-9]+)", re.S)
PAINT = frozenset((b"f", b"F", b"f*", b"S", b"s", b"B", b"B*", b"b", b"b*"))
PATH = frozenset((b"m", b"l", b"c", b"v", b"y", b"h", b"re"))
BLOCKERS = {
    "text": frozenset((b"BT", b"ET", b"Tj", b"TJ", b"Tf", b"Td", b"Tm", b"T*")),
    "xobject": frozenset((b"Do",)),
    "inline_image": frozenset((b"BI", b"ID", b"EI")),
    "shading": frozenset((b"sh",)),
    "marked_content": frozenset((b"BDC", b"BMC", b"EMC")),
}


def content_tokens(data):
    """Lex bare tokens, avoiding comments, PDF names, literal/hex strings.

    This is deliberately not a full PDF operand or graphics-state parser.
    """
    pos = 0
    while pos < len(data):
        c = data[pos]
        if c in b"\x00\x09\x0a\x0c\x0d\x20[]{}":
            pos += 1
            continue
        if c == 37:  # percent comment
            while pos < len(data) and data[pos] not in b"\r\n":
                pos += 1
            continue
        if c == 40:  # nested PDF literal string
            pos += 1
            depth = 1
            while pos < len(data) and depth:
                if data[pos] == 92:  # escape next byte
                    pos += 2
                    continue
                if data[pos] == 40:
                    depth += 1
                elif data[pos] == 41:
                    depth -= 1
                pos += 1
            continue
        if c == 60:  # hex string or dictionary start
            if data[pos:pos + 2] == b"<<":
                pos += 2
            else:
                pos += 1
                end = data.find(b">", pos)
                pos = len(data) if end < 0 else end + 1
            continue
        if c == 62:  # dictionary close
            pos += 2 if data[pos:pos + 2] == b">>" else 1
            continue
        if c == 47:  # slash-prefixed PDF name, not an operator
            pos += 1
            while pos < len(data) and data[pos] not in b"\x00\x09\x0a\x0c\x0d\x20[]{}<>()/%":
                pos += 1
            continue
        if c in b")":
            pos += 1
            continue
        start = pos
        while pos < len(data) and data[pos] not in b"\x00\x09\x0a\x0c\x0d\x20[]{}<>()/%":
            pos += 1
        if pos > start:
            yield data[start:pos]
        else:
            pos += 1


def _filter_kind(header):
    # Flate output is a PDF operator stream only with identity prediction.
    # PNG/TIFF /Predictor and indirect DecodeParms require a real PDF
    # decoder; blindly tokenizing their residual bytes can yield a false
    # 'no W*' verdict. They are explicitly INCONCLUSIVE instead.
    if header.count(b"/DecodeParms") > 1:
        return "unsupported_filter"
    if b"/DecodeParms" in header:
        parameters = header.split(b"/DecodeParms", 1)[1].lstrip()
        if parameters.startswith(b"<<"):
            end = parameters.find(b">>")
            if end < 0:
                return "unsupported_filter"
            values = re.findall(rb"/Predictor\s+(\d+)\b",
                                parameters[:end])
            if len(values) > 1 or (values and values[0] != b"1"):
                return "unsupported_filter"
        elif not parameters.startswith(b"null"):
            return "unsupported_filter"
    matches = list(FILTER.finditer(header))
    # Nested dictionaries may have their own Filter entries. Do not
    # silently select the last one and pretend it describes this stream.
    if len(matches) > 1:
        return "unsupported_filter"
    if not matches:
        return "raw"
    selected = matches[0].group(1).strip()
    names = (tuple(selected[1:-1].split())
             if selected.startswith(b"[") and selected.endswith(b"]")
             else (selected,))
    allowed = {
        (b"/FlateDecode",): "flate",
        (b"/ASCII85Decode",): "ascii85",
        (b"/ASCII85Decode", b"/FlateDecode"): "ascii85_flate",
    }
    # Filter arrays execute in the declared order. DecodeParms arrays need
    # positional semantics we do not implement; never guess them.
    if b"/ASCII85Decode" in names and b"/DecodeParms" in header:
        return "unsupported_filter"
    return allowed.get(names, "unsupported_filter")


def _stream_header(blob, marker_start):
    # The last << may be a nested /DecodeParms dictionary and may follow
    # /Filter /FlateDecode. Limit to the actual indirect object's header.
    prefix = blob[max(0, marker_start - 4096):marker_start]
    objects = list(OBJECT_HEADER.finditer(prefix))
    if not objects:
        return None, "unresolved_stream_object"
    header = prefix[objects[-1].end():]
    lengths = list(DIRECT_LENGTH.finditer(header))
    if len(lengths) != 1 or lengths[0].group(2) is not None:
        return None, "indirect_or_ambiguous_length"
    length = int(lengths[0].group(1))
    if length > MAX_INPUT:
        return None, "stream_exceeds_budget"
    return (header, length), None


def _inflate_bounded(payload):
    decoder = zlib.decompressobj()
    data = decoder.decompress(payload, MAX_DECODED_STREAM + 1)
    if (len(data) > MAX_DECODED_STREAM or not decoder.eof
            or decoder.unused_data or decoder.unconsumed_tail):
        raise ValueError("exceeded decoded stream limit or malformed Flate data")
    trailer = decoder.flush()
    if len(data) + len(trailer) > MAX_DECODED_STREAM:
        raise ValueError("decoded stream exceeds limit")
    return data + trailer


def _ascii85_bounded(payload):
    # PDF ASCII85 streams normally end in ~> without the optional <~ prefix.
    # Reject oversized decoded intermediates before any Flate pass.
    if len(payload) > 2 * MAX_DECODED_STREAM + 64:
        raise ValueError("encoded ASCII85 stream exceeds screening limit")
    decoded = base64.a85decode(payload.strip(b" \t\r\n\v\f"), adobe=True)
    if len(decoded) > MAX_DECODED_STREAM:
        raise ValueError("decoded ASCII85 stream exceeds screening limit")
    return decoded


def screen_pdf_bytes(blob):
    if len(blob) > MAX_INPUT:
        raise ValueError("original PDF exceeds bounded input limit")
    if not blob.startswith(b"%PDF-") or b"%%EOF" not in blob[-1024:]:
        raise ValueError("invalid PDF framing")
    stream_count = decoded_count = 0
    stream_issues = []
    counts = {"w": 0, "wstar": 0, "paint": 0, "path_commands": 0}
    blocker_counts = {name: 0 for name in BLOCKERS}
    position = 0
    while True:
        match = STREAM_MARKER.search(blob, position)
        if match is None:
            break
        stream_count += 1
        if stream_count > MAX_STREAMS:
            stream_issues.append("too_many_streams")
            break
        spec, failure = _stream_header(blob, match.start())
        if failure is not None:
            stream_issues.append(failure)
            break
        header, length = spec
        end = match.end() + length
        if end > len(blob):
            stream_issues.append("truncated_stream")
            break
        # /Length is the authority: the bytes inside a stream can contain
        # the ASCII word endstream, including in PDF literal strings.
        boundary = re.match(rb"[ \t\r\n]*endstream\b", blob[end:end + 32])
        if boundary is None:
            stream_issues.append("stream_length_or_boundary_mismatch")
            break
        payload = blob[match.end():end]
        kind = _filter_kind(header)
        position = end + boundary.end()
        if kind == "unsupported_filter":
            stream_issues.append("unsupported_stream_filter")
            continue
        try:
            if kind == "flate":
                decoded = _inflate_bounded(payload)
            elif kind == "ascii85":
                decoded = _ascii85_bounded(payload)
            elif kind == "ascii85_flate":
                decoded = _inflate_bounded(_ascii85_bounded(payload))
            else:
                decoded = payload
        except (ValueError, OverflowError, zlib.error):
            stream_issues.append(
                "ascii85_decode_failed_or_over_budget"
                if kind.startswith("ascii85")
                else "flate_decode_failed_or_over_budget"
            )
            continue
        if len(decoded) > MAX_DECODED_STREAM:
            stream_issues.append("decoded_stream_over_budget")
            continue
        decoded_count += 1
        for tok in content_tokens(decoded):
            if tok == b"W*":
                counts["wstar"] += 1
            elif tok == b"W":
                counts["w"] += 1
            if tok in PAINT:
                counts["paint"] += 1
            if tok in PATH:
                counts["path_commands"] += 1
            for name, operators in BLOCKERS.items():
                if tok in operators:
                    blocker_counts[name] += 1
    if stream_count == 0:
        stream_issues.append("no_streams_found")
    blockers = sorted(name for name, value in blocker_counts.items() if value)
    if stream_issues:
        status = "inconclusive"
    elif counts["wstar"] == 0:
        status = "no_wstar_observed"
    elif blockers:
        status = "wstar_with_unsupported_operations"
    elif not counts["paint"] or not counts["path_commands"]:
        status = "wstar_without_observed_path_paint"
    else:
        status = "candidate_requires_full_page_preflight"
    return {
        "status": status,
        "stream_count": stream_count,
        "decoded_stream_count": decoded_count,
        "stream_issues": sorted(set(stream_issues)),
        "operator_counts": counts,
        "unsupported_feature_classes": blockers,
        "unsupported_feature_counts": blocker_counts,
        "native_wstar_clip_supported_count": 0,
        "native_pixel_conformance": "not_run",
        "license_review": "required",
        "page_content_attribution": "not_verified",
    }


def screen_pdf_file(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError("source must be an ordinary non-symlink file")
    if path.stat().st_size > MAX_INPUT:
        raise ValueError("source exceeds bounded input limit")
    original_bytes = path.read_bytes()
    result = screen_pdf_bytes(original_bytes)
    return {"filename": path.name, "size_bytes": len(original_bytes),
            "sha256": hashlib.sha256(original_bytes).hexdigest(), **result}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pdf", action="append", type=Path, required=True,
                        help="existing unchanged original PDF (repeatable)")
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args(argv)
    paths = args.pdf
    if not 1 <= len(paths) <= MAX_PDFS or len({p.name for p in paths}) != len(paths):
        parser.error("1..128 distinct input PDF basenames required")
    input_paths = {p.resolve() for p in paths}
    if args.report.resolve() in input_paths or Path(str(args.report) + ".tmp").resolve() in input_paths:
        parser.error("screening report would replace an original PDF")
    if args.report.is_symlink():
        parser.error("report symlink rejected")
    staged = Path(str(args.report) + ".tmp")
    if staged.is_symlink():
        parser.error("temporary report symlink rejected")
    args.report.unlink(missing_ok=True)
    staged.unlink(missing_ok=True)
    try:
        docs = [screen_pdf_file(p) for p in paths]
        report = {"schema_version": 1, "result": "screening_only",
                  "native_wstar_clip_supported_count": 0,
                  "documents": sorted(docs, key=lambda doc: doc["filename"])}
        with staged.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(report, stream, indent=2, sort_keys=True)
            stream.write("\n")
        staged.replace(args.report)
    except (ValueError, OSError) as exc:
        staged.unlink(missing_ok=True)
        parser.error("external W* screening failed: " + str(exc))
    print("[external-wstar-screen] screening_only documents=" + str(len(docs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
