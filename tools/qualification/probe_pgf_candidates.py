#!/usr/bin/env python3
"""Conservative Issue #262 W* discovery over unchanged, quarantined PDFs.

Unlike the old byte-regex probe, this resolves xref/object streams, indirect
/Contents, filters and nested Form XObjects. Results are discovery-only; formal
admission still requires rights and original-PDFium/native-C11 pixel evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import logging
from pathlib import Path
from typing import Any

from pypdf import PdfReader
from pypdf.errors import PdfReadError
from pypdf.generic import ArrayObject, ContentStream, DictionaryObject, StreamObject

ROOT = Path("tests/external_wstar_candidates")
MAX_BYTES = 20 * 1024 * 1024
MAX_PAGES = 128
MAX_FORM_DEPTH = 16
MAX_OPERATIONS = 200_000

PATH = {b"m", b"l", b"c", b"v", b"y", b"h", b"re"}
END_PATH = {b"n", b"S", b"s", b"f", b"F", b"f*", b"B", b"B*", b"b", b"b*"}
TEXT = {
    b"BT", b"ET", b"Tc", b"Tw", b"Tz", b"TL", b"Tf", b"Tr", b"Ts",
    b"Td", b"TD", b"Tm", b"T*", b"Tj", b"TJ", b"'", b'"',
    b"d0", b"d1",
}
BASIC = PATH | END_PATH | {
    b"W", b"W*", b"q", b"Q", b"cm", b"w", b"J", b"j", b"M", b"d",
    b"ri", b"i", b"g", b"G", b"rg", b"RG", b"k", b"K", b"cs", b"CS",
}
PDF_WHITESPACE = frozenset(b"\x00\t\n\f\r ")


class Incomplete(Exception):
    """Incomplete parsing cannot be used as evidence of zero W* operators."""


class WarningRecorder(logging.Handler):
    def __init__(self) -> None:
        super().__init__(logging.WARNING)
        self.messages: list[str] = []

    def emit(self, record: logging.LogRecord) -> None:
        if record.name.startswith("pypdf"):
            self.messages.append(record.getMessage()[:240])


def resolved(item: Any) -> Any:
    return item.get_object() if hasattr(item, "get_object") else item


def require_dict(item: Any, context: str) -> DictionaryObject:
    value = resolved(item)
    if not isinstance(value, DictionaryObject):
        raise Incomplete(f"{context}: expected dictionary, got {type(value).__name__}")
    return value


def source_identity(item: Any) -> str:
    ref = getattr(item, "indirect_reference", None)
    return f"{ref.idnum}:{ref.generation}" if ref else f"direct:{id(item)}"


def path_evidence(path: list[bytes]) -> dict[str, Any]:
    rectangles, moves = path.count(b"re"), path.count(b"m")
    nonrect = any(x in path for x in (b"l", b"c", b"v", b"y"))
    return {
        "rectangles": rectangles,
        "moves": moves,
        "has_curves_or_lines": nonrect,
        # Only a ranking heuristic, NEVER positive even-odd geometry proof.
        "potential_evenodd_difference": rectangles + moves >= 2 or nonrect,
        "requires_geometry_and_pixel_review": True,
    }


def record_risk(details: dict[str, Any], operator_name: str, risk: str) -> None:
    details["native_raster_risks"].append(risk)
    counts = details["risk_operator_counts"]
    counts[operator_name] = counts.get(operator_name, 0) + 1


def scan_pdf(data: bytes, *, allow_nonzero_xref_warning: bool = False) -> dict[str, Any]:
    """Fail closed on any undecodable or incompletely traversed page content."""
    result: dict[str, Any] = {
        "scan_status": "inconclusive",
        "reason": None,
        "pdf_header_offset_bytes": None,
        "page_count": None,
        "page_content_wstar_count": None,
        "form_wstar_count": None,
        "effective_wstar_count": None,
        "page_text_operator_count": None,
        "native_subgraph_review_required": True,
        "pages": [],
    }
    if len(data) > MAX_BYTES:
        result["reason"] = "size_limit"
        return result
    # Some published PDFs begin with PDF whitespace before the header. Preserve
    # the original bytes and xref offsets; only permit a bounded whitespace
    # prefix, then let pypdf parse non-strictly and audit every warning below.
    header_offset = data.find(b"%PDF-")
    result["pdf_header_offset_bytes"] = header_offset if header_offset >= 0 else None
    if (header_offset < 0 or header_offset > 1024 or
            any(byte not in PDF_WHITESPACE for byte in data[:header_offset])):
        result["reason"] = "invalid_pdf_header_prefix"
        return result
    recorder = WarningRecorder()
    logger = logging.getLogger("pypdf")
    logger.addHandler(recorder)
    try:
        # The xref-repair exception is only for a separately qpdf-validated
        # candidate workflow; default scans remain strict and fail closed.
        reader = PdfReader(io.BytesIO(data),
                           strict=(header_offset == 0 and not allow_nonzero_xref_warning))
        if reader.is_encrypted:
            raise Incomplete("encrypted_document")
        pages = reader.pages
        count = len(pages)
        if count > MAX_PAGES:
            raise Incomplete("page_limit")
        result["page_count"] = count
        for index, page in enumerate(pages):
            details: dict[str, Any] = {
                "index": index,
                "content_stream_wstar": 0,
                "form_xobject_wstar": 0,
                "text_operators": 0,
                "inline_images": 0,
                "image_xobject_calls": 0,
                "clip_evidence": [],
                "native_raster_risks": [],
                "operator_counts": {},
                "risk_operator_counts": {},
            }
            result["pages"].append(details)
            active_forms: set[str] = set()
            used = [0]

            def analyze(content: Any, resources: Any, origin: str, depth: int) -> None:
                if depth > MAX_FORM_DEPTH:
                    raise Incomplete("form_depth_limit")
                res = require_dict(resources, "effective /Resources")
                obj = resolved(content)
                if not isinstance(obj, (StreamObject, ArrayObject)):
                    raise Incomplete(f"{origin}: non-stream /Contents")
                # ContentStream concatenates /Contents arrays and uses actual PDF
                # syntax rather than matching operator-like bytes in strings.
                parsed = ContentStream(obj, reader).operations
                path: list[bytes] = []
                for operands, operator in parsed:
                    operator_name = operator.decode("latin-1", errors="replace")
                    counts = details["operator_counts"]
                    counts[operator_name] = counts.get(operator_name, 0) + 1
                    used[0] += 1
                    if used[0] > MAX_OPERATIONS:
                        raise Incomplete("operation_limit")
                    if operator == b"W*":
                        field = "content_stream_wstar" if depth == 0 else "form_xobject_wstar"
                        details[field] += 1
                        details["clip_evidence"].append({
                            "origin": origin, "operator": "W*", **path_evidence(path)
                        })
                    elif operator in PATH:
                        path.append(operator)
                    elif operator in END_PATH:
                        path.clear()
                    elif operator in TEXT:
                        details["text_operators"] += 1
                        record_risk(details, operator_name, "text")
                    elif operator == b"INLINE IMAGE":
                        details["inline_images"] += 1
                        record_risk(details, operator_name, "inline_image")
                    elif operator == b"Do":
                        if len(operands) != 1:
                            raise Incomplete(f"{origin}: malformed Do")
                        xobjects = require_dict(res.get("/XObject"), f"{origin}: /XObject")
                        name = operands[0]
                        if name not in xobjects:
                            raise Incomplete(f"{origin}: missing XObject {name}")
                        target_ref = xobjects.raw_get(name)
                        target = resolved(target_ref)
                        if not isinstance(target, StreamObject):
                            raise Incomplete(f"{origin}: non-stream XObject")
                        subtype = target.get("/Subtype")
                        if subtype == "/Image":
                            details["image_xobject_calls"] += 1
                            record_risk(details, operator_name, "image_xobject")
                        elif subtype == "/Form":
                            record_risk(details, operator_name, "form_xobject")
                            identity = (f"{target_ref.idnum}:{target_ref.generation}"
                                        if hasattr(target_ref, "idnum")
                                        else source_identity(target))
                            if identity in active_forms:
                                raise Incomplete(f"{origin}: cyclic Form XObject")
                            active_forms.add(identity)
                            try:
                                analyze(target, target.get("/Resources", res),
                                        f"{origin}/Form({name})", depth + 1)
                            finally:
                                active_forms.remove(identity)
                        else:
                            raise Incomplete(f"{origin}: unknown XObject subtype {subtype}")
                    elif operator == b"gs":
                        record_risk(details, operator_name, "ext_gstate")
                    elif operator in {b"sh", b"scn", b"SCN", b"MP", b"DP", b"BMC",
                                      b"BDC", b"EMC", b"BX", b"EX"}:
                        record_risk(details, operator_name, "other_graphics_operator")
                    elif operator not in BASIC:
                        record_risk(details, operator_name, "unrecognized_operator")

            if "/Contents" in page:
                # Missing page /Contents is valid blank content, but malformed
                # explicitly present content must NOT become a zero hit.
                analyze(page.raw_get("/Contents"),
                        page.get("/Resources", DictionaryObject()), "page", 0)
            if page.get("/Annots"):
                # Annotation appearance streams can paint content and W* too.
                # Until their appearance/resource graph is traversed, a zero-hit
                # result for this document would be incomplete evidence.
                raise Incomplete("annotations_not_scanned")
            details["native_raster_risks"] = sorted(set(details["native_raster_risks"]))
            details["operator_counts"] = dict(sorted(details["operator_counts"].items()))
            details["risk_operator_counts"] = dict(sorted(details["risk_operator_counts"].items()))

        if recorder.messages:
            benign_header_warnings = (
                header_offset > 0 and bool(recorder.messages) and
                all(message.lower().startswith("invalid pdf header:")
                    for message in recorder.messages)
            )
            benign_validated_xref_repair = (
                allow_nonzero_xref_warning and len(recorder.messages) == 1 and
                recorder.messages[0].lower().startswith(
                    "xref table not zero-indexed. id numbers for objects will be corrected."
                )
            )
            if not (benign_header_warnings or benign_validated_xref_repair):
                raise Incomplete("pypdf_parser_warning: " +
                                 " | ".join(recorder.messages[:3]))
        result["page_content_wstar_count"] = sum(
            x["content_stream_wstar"] for x in result["pages"]
        )
        result["form_wstar_count"] = sum(
            x["form_xobject_wstar"] for x in result["pages"]
        )
        result["effective_wstar_count"] = (
            result["page_content_wstar_count"] + result["form_wstar_count"]
        )
        result["page_text_operator_count"] = sum(
            x["text_operators"] for x in result["pages"]
        )
        result["scan_status"] = (
            "confirmed_no_wstar" if result["effective_wstar_count"] == 0
            else "wstar_found"
        )
    except Exception as exc:
        # This is a research scanner, not the native decoder. A failed parse
        # must yield an explicit unknown classification rather than zero W*.
        result["reason"] = f"{type(exc).__name__}: {str(exc)[:300]}"
    finally:
        logger.removeHandler(recorder)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, help="optional JSON report")
    parser.add_argument(
        "--allow-nonzero-xref-warning",
        action="store_true",
        help="allow only pypdf's xref-subsection repair warning; use only after qpdf --check passes",
    )
    args = parser.parse_args()
    records = []
    for path in sorted(args.root.glob("*.pdf")):
        raw = path.read_bytes()
        records.append({
            "path": str(path),
            "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest(),
            **scan_pdf(raw, allow_nonzero_xref_warning=args.allow_nonzero_xref_warning),
        })
    output = {
        "schema_version": 2,
        "status": "discovery_only_not_official_corpus",
        "files_scanned": len(records),
        "confirmed_no_wstar": sum(
            x["scan_status"] == "confirmed_no_wstar" for x in records
        ),
        "wstar_found": sum(x["scan_status"] == "wstar_found" for x in records),
        "inconclusive": sum(
            x["scan_status"] == "inconclusive" for x in records
        ),
        "results": records,
    }
    payload = json.dumps(output, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload, encoding="utf-8")
    print(payload, end="")


if __name__ == "__main__":
    main()
