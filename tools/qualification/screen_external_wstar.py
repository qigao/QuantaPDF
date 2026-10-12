#!/usr/bin/env python3
"""Bounded, offline *screening only* of original PDFs for W* path clips.

Not a PDF parser, provenance review, license grant, native rendering test, or
PDFium oracle. No result from this tool can increment conformance counters.
"""

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import zlib

MAX_PDFS = 128
MAX_INPUT = 16 * 1024 * 1024
MAX_STREAMS = 64
MAX_DECODED_STREAM = 1024 * 1024
MAX_DIRECTORY_ENTRIES = 4096
MAX_DIRECTORY_DEPTH = 16
MAX_INTEGER_OBJECT_HEADERS = 4096
STREAM_MARKER = re.compile(rb"(?<![A-Za-z])stream(?:\r\n|\n|\r)")
OBJECT_HEADER = re.compile(rb"(?:^|[\r\n])\s*\d+\s+\d+\s+obj\b")
PDF_HEADER = re.compile(rb"%PDF-(?:1\.[0-7]|2\.0)(?:\r\n|\r|\n)")
DIRECT_LENGTH = re.compile(rb"/Length\s+(\d+)(?:\s+(\d+)\s+R)?\b")
INDIRECT_OBJECT_HEADER = re.compile(
    rb"(?:^|[\r\n])[ \t]*(\d+)[ \t]+(\d+)[ \t]+obj\b"
)
SIMPLE_INTEGER_OBJECT = re.compile(
    rb"[ \t\r\n]+([0-9]{1,8})[ \t\r\n]+endobj\b"
)
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
        (b"/ASCIIHexDecode",): "asciihex",
        (b"/ASCIIHexDecode", b"/FlateDecode"): "asciihex_flate",
    }
    # Filter arrays execute in the declared order. DecodeParms arrays need
    # positional semantics we do not implement; never guess them.
    if (any(name in names for name in (b"/ASCII85Decode", b"/ASCIIHexDecode"))
            and b"/DecodeParms" in header):
        return "unsupported_filter"
    return allowed.get(names, "unsupported_filter")


def _simple_length_object_index(blob):
    """Index uniquely named simple integer objects, without xref guessing.

    Duplicate revisions, complex references or excessive object counts are
    deliberately ambiguous. This is discovery, not native PDF admission.
    """
    result = {}
    for index, match in enumerate(INDIRECT_OBJECT_HEADER.finditer(blob)):
        if index >= MAX_INTEGER_OBJECT_HEADERS:
            return None
        key = (int(match.group(1)), int(match.group(2)))
        if key in result:
            result[key] = None
            continue
        body = blob[match.end():match.end() + 96]
        value = SIMPLE_INTEGER_OBJECT.match(body)
        result[key] = int(value.group(1)) if value else None
    return result


def _stream_header(blob, marker_start, length_objects):
    # The last << may be a nested /DecodeParms dictionary and may follow
    # /Filter /FlateDecode. Limit to the actual indirect object's header.
    prefix = blob[max(0, marker_start - 4096):marker_start]
    objects = list(OBJECT_HEADER.finditer(prefix))
    if not objects:
        return None, "unresolved_stream_object"
    header = prefix[objects[-1].end():]
    lengths = list(DIRECT_LENGTH.finditer(header))
    if len(lengths) != 1:
        return None, "indirect_or_ambiguous_length"
    number, generation = lengths[0].group(1), lengths[0].group(2)
    if generation is None:
        length = int(number)
    else:
        key = (int(number), int(generation))
        length = (length_objects.get(key)
                  if length_objects is not None else None)
        if length is None:
            return None, "indirect_or_ambiguous_length"
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


def _asciihex_bounded(payload):
    # PDF ASCIIHex streams end in '>'; a final unpaired nibble gets a zero
    # low nibble. Non-whitespace after the terminator is invalid.
    if len(payload) > 2 * MAX_DECODED_STREAM + 64:
        raise ValueError("encoded ASCIIHex stream exceeds screening limit")
    decoded = bytearray()
    high = None
    terminated = False
    for char in payload:
        if char in b"\x00\x09\x0a\x0c\x0d\x20":
            continue
        if char == 62:  # '>'
            if terminated:
                raise ValueError("duplicate ASCIIHex terminator")
            terminated = True
            continue
        if terminated:
            raise ValueError("trailing ASCIIHex data")
        if 48 <= char <= 57:
            digit = char - 48
        elif 65 <= char <= 70:
            digit = char - 55
        elif 97 <= char <= 102:
            digit = char - 87
        else:
            raise ValueError("non-hex ASCIIHex byte")
        if high is None:
            high = digit
        else:
            decoded.append((high << 4) | digit)
            high = None
            if len(decoded) > MAX_DECODED_STREAM:
                raise ValueError("decoded ASCIIHex stream exceeds screening limit")
    if not terminated:
        raise ValueError("missing ASCIIHex terminator")
    if high is not None:
        decoded.append(high << 4)
    if len(decoded) > MAX_DECODED_STREAM:
        raise ValueError("decoded ASCIIHex stream exceeds screening limit")
    return bytes(decoded)


def screen_pdf_bytes(blob):
    if len(blob) > MAX_INPUT:
        raise ValueError("original PDF exceeds bounded input limit")
    header = PDF_HEADER.search(blob, 0, min(len(blob), 1024 + 9))
    if (header is None or header.start() >= 1024
            or b"%%EOF" not in blob[-1024:]):
        raise ValueError("invalid PDF framing")
    pdf_header_offset = header.start()
    stream_count = decoded_count = 0
    stream_issues = []
    counts = {"w": 0, "wstar": 0, "paint": 0, "path_commands": 0}
    blocker_counts = {name: 0 for name in BLOCKERS}
    # Any printer wrapper or non-PDF preamble is not a PDF content stream.
    # Do not read its fake object headers or W* tokens as page evidence.
    position = pdf_header_offset
    # Build the bounded integer-object index once, not once per stream.
    length_objects = _simple_length_object_index(blob[pdf_header_offset:])
    while True:
        match = STREAM_MARKER.search(blob, position)
        if match is None:
            break
        stream_count += 1
        if stream_count > MAX_STREAMS:
            stream_issues.append("too_many_streams")
            break
        spec, failure = _stream_header(blob, match.start(), length_objects)
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
            elif kind == "asciihex":
                decoded = _asciihex_bounded(payload)
            elif kind == "asciihex_flate":
                decoded = _inflate_bounded(_asciihex_bounded(payload))
            else:
                decoded = payload
        except (ValueError, OverflowError, zlib.error):
            stream_issues.append(
                "ascii85_decode_failed_or_over_budget"
                if kind.startswith("ascii85")
                else ("asciihex_decode_failed_or_over_budget"
                      if kind.startswith("asciihex")
                      else "flate_decode_failed_or_over_budget")
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
        "pdf_header_offset": pdf_header_offset,
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


def _discover_pdf_directory(root):
    """Enumerate bounded, deterministic local PDF files without following links.

    This is a source-*discovery* convenience only, not native admission.
    """
    if root.is_symlink() or not root.is_dir():
        raise ValueError("PDF directory must be a real, non-symlink directory")
    base = root.resolve()
    pending = [(root, 0)]
    found = []
    entries_seen = 0
    while pending:
        current, depth = pending.pop()
        with os.scandir(current) as scan:
            entries = sorted(scan, key=lambda item: item.name)
        for entry in entries:
            entries_seen += 1
            if entries_seen > MAX_DIRECTORY_ENTRIES:
                raise ValueError("PDF directory exceeds entry budget")
            path = Path(entry.path)
            # Never follow a symlink, including symlinked directories.
            if entry.is_symlink():
                if path.suffix.lower() == ".pdf":
                    raise ValueError("symlink PDF candidate rejected")
                continue
            if entry.is_dir(follow_symlinks=False):
                if depth >= MAX_DIRECTORY_DEPTH:
                    raise ValueError("PDF directory exceeds nesting budget")
                pending.append((path, depth + 1))
            elif path.suffix.lower() == ".pdf":
                if not entry.is_file(follow_symlinks=False):
                    raise ValueError("PDF candidate is not a regular file")
                if base not in path.resolve().parents:
                    raise ValueError("PDF candidate leaves input directory")
                found.append(path)
                if len(found) > MAX_PDFS:
                    raise ValueError("PDF directory exceeds document budget")
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pdf", action="append", type=Path,
                        help="existing unchanged original PDF (repeatable)")
    parser.add_argument("--pdf-dir", action="append", type=Path,
                        help="bounded recursive offline PDF directory (repeatable)")
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args(argv)
    paths = list(args.pdf or [])
    try:
        for directory in args.pdf_dir or []:
            paths.extend(_discover_pdf_directory(directory))
            if len(paths) > MAX_PDFS:
                raise ValueError("PDF input count exceeds document budget")
    except (OSError, ValueError) as exc:
        parser.error("external W* directory discovery failed: " + str(exc))
    if not 1 <= len(paths) <= MAX_PDFS or len({p.name for p in paths}) != len(paths):
        parser.error("1..128 distinct input PDF basenames required")
    input_paths = {p.resolve() for p in paths}
    staged = Path(str(args.report) + ".tmp")
    if args.report.resolve() in input_paths or staged.resolve() in input_paths:
        parser.error("screening report would replace an original PDF")
    if args.report.is_symlink():
        parser.error("report symlink rejected")
    if staged.is_symlink():
        parser.error("temporary report symlink rejected")
    # Resolve inode aliases as well as canonical paths. A hardlinked report
    # can be the exact original PDF under another filename.
    for output in (args.report, staged):
        if output.exists():
            for source in paths:
                if os.path.samefile(output, source):
                    parser.error("screening output hardlinks an original PDF")
    try:
        docs = [screen_pdf_file(p) for p in paths]
        # Stage only after all input bytes have passed validation.
        # A pre-existing report is never unlinked on an input failure.
        staged.unlink(missing_ok=True)
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
