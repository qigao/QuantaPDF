#!/usr/bin/env python3
"""Reproduce and reconcile test-authored original PDF byte corpus for Source V10.

These are generated test fixtures, not independently licensed external PDFs.
No web access, PDFium, QPDF writer, fallback, or approximate raster oracle.
Byte-level PDF authoring is TEST INPUT generation only; QuantaPDF remains
sole content interpreter and raster producer in the consumer CTest.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import sys

CORPUS_ID = "native-v10-cross-document-v1"
KIND = "generated-test-owned"
PROVENANCE = "QuantaPDF-test-authored-original-bytes-v1"
MAX_BYTES = 8192
SEEDS = (0x4CF5AD11, 0x9E3779B9)
EXPECTED_FINGERPRINTS = ("2b45cb5f773552a9", "8e303671b5772919",
                         "374a00fcf10a1e89")
ID_RE = re.compile(r"[a-z][a-z0-9-]{2,50}\Z")
HASH_RE = re.compile(r"[0-9a-f]{64}\Z")
MASK32 = (1 << 32) - 1
MASK64 = (1 << 64) - 1
DOCUMENT_FIELDS = {
    "id", "sha256", "size_bytes", "source_provenance", "feature_tags",
    "page_index", "expected_status", "expected_center_rgb", "width_points",
    "height_points", "reference_oracle"
}


def fail(reason: str) -> None:
    raise ValueError(reason)


def unique_pairs(pairs: list[tuple[str, object]]) -> dict:
    obj: dict = {}
    for key, value in pairs:
        if key in obj:
            fail("duplicate JSON key: " + key)
        obj[key] = value
    return obj


def nonfinite(token: str):
    fail("nonfinite JSON token: " + token)


def read_json(path: Path, limit: int) -> dict:
    if path.is_symlink() or not path.is_file() or path.stat().st_size > limit:
        fail("missing/symlink/oversize JSON: " + str(path))
    o = json.loads(path.read_bytes().decode("utf-8"), object_pairs_hook=unique_pairs,
                   parse_constant=nonfinite)
    if type(o) is not dict:
        fail("expected JSON object")
    return o


def content_stream(data: bytes) -> bytes:
    return b"<< /Length " + str(len(data)).encode("ascii") + b" >>\nstream\n" + data + b"\nendstream"


def original_pdf(content: bytes, resource: str = "<< >>",
                 extra: bytes | None = None) -> bytes:
    """Only fixture-owned literal PDF objects; offsets and sizes are exact."""
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        ("<< /Type /Pages /Kids [3 0 R] /Count 1 "
         "/MediaBox [0 0 16 12] /Resources " + resource + " >>").encode("ascii"),
        b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>",
        content_stream(content),
    ]
    if extra is not None:
        objects.append(extra)
    buf = bytearray(b"%PDF-1.7\n%QuantaPDF-test-owned\n")
    offsets = [0]
    for n, obj in enumerate(objects, 1):
        offsets.append(len(buf))
        buf += f"{n} 0 obj\n".encode("ascii") + obj + b"\nendobj\n"
    startxref = len(buf)
    buf += f"xref\n0 {len(objects) + 1}\n0000000000 65535 f \n".encode("ascii")
    for offset in offsets[1:]:
        if offset >= 10**10:
            fail("xref overflow")
        buf += f"{offset:010d} 00000 n \n".encode("ascii")
    buf += (f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
            f"startxref\n{startxref}\n%%EOF\n").encode("ascii")
    return bytes(buf)


def image_object(color: bytes, colorspace: str = "/DeviceRGB",
                 subtype: str = "/Image") -> bytes:
    if subtype == "/Form":
        return b"<< /Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources << >> /Length 0 >>\nstream\n\nendstream"
    return ("<< /Type /XObject /Subtype " + subtype +
            " /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace " +
            colorspace + " /Length " + str(len(color)) + " >>\nstream\n").encode("ascii") + color + b"\nendstream"


DRAW = b"q 16 0 0 12 0 0 cm /I Do Q "
CLIPPED = b"q 2 2 10 8 re W n " + DRAW + b"Q "
RED_PATH = b"1 0 0 rg 0 0 16 12 re f "
BLUE_PATH = b"0 0 1 rg 0 0 16 12 re f "
RESOURCE_IMAGE = "<< /XObject << /I 5 0 R >> >>"
RESOURCE_DEFAULT = ("<< /ColorSpace << /DefaultRGB /DeviceGray >> "
                    "/XObject << /I 5 0 R >> >>")


def specimens() -> dict[str, tuple[bytes, str, str | None, list[str]]]:
    # Literal origin and independently authored expected status/center RGB.
    return {
        "path-red": (original_pdf(RED_PATH), "OK", "ff0000", ["path", "red"]),
        "path-blue": (original_pdf(BLUE_PATH), "OK", "0000ff", ["path", "blue"]),
        "image-red": (original_pdf(DRAW, RESOURCE_IMAGE,
                                   image_object(bytes([255, 0, 0]))),
                      "OK", "ff0000", ["image", "resource-name-i", "red"]),
        "image-green": (original_pdf(DRAW, RESOURCE_IMAGE,
                                     image_object(bytes([0, 255, 0]))),
                        "OK", "00ff00", ["image", "resource-name-i", "green"]),
        "image-clipped-blue": (original_pdf(CLIPPED, RESOURCE_IMAGE,
                                            image_object(bytes([0, 0, 255]))),
                               "OK", "0000ff", ["image", "clip-w", "blue"]),
        "blank-page": (original_pdf(b""), "OK", "ffffff", ["blank", "white"]),
        "reject-late-text": (original_pdf(RED_PATH + b"BT ET "),
                             "UNSUPPORTED", None, ["path", "text", "late-rejection"]),
        "reject-missing-name": (original_pdf(
            b"q 16 0 0 12 0 0 cm /Missing Do Q ", RESOURCE_IMAGE,
            image_object(bytes([255, 0, 0]))),
                                "FORMAT", None, ["image", "missing-name", "atomic"]),
        "reject-form-xobject": (original_pdf(DRAW, RESOURCE_IMAGE,
                                             image_object(b"", subtype="/Form")),
                                "UNSUPPORTED", None, ["form-xobject", "atomic"]),
        "reject-default-rgb": (original_pdf(DRAW, RESOURCE_DEFAULT,
                                            image_object(bytes([255, 0, 0]))),
                               "UNSUPPORTED", None, ["default-rgb", "atomic"]),
        "reject-image-late-text": (original_pdf(
            DRAW + b"BT ET ", RESOURCE_IMAGE,
            image_object(bytes([0, 255, 0]))),
                                   "UNSUPPORTED", None, ["image", "late-text", "atomic"]),
        "reject-cmyk-image": (original_pdf(
            DRAW, RESOURCE_IMAGE, image_object(bytes([0, 0, 0, 0]), "/DeviceCMYK")),
                                "UNSUPPORTED", None, ["image", "cmyk", "atomic"]),
    }


def order_shuffle(names: list[str], seed: int) -> list[str]:
    if seed == 0:
        fail("zero shuffle seed")
    result = list(names)
    state = seed
    for size in range(len(result), 1, -1):
        state = (state ^ ((state << 13) & MASK32)) & MASK32
        state = (state ^ (state >> 17)) & MASK32
        state = (state ^ ((state << 5) & MASK32)) & MASK32
        j = state % size
        result[size - 1], result[j] = result[j], result[size - 1]
    return result


def order_fingerprint(seq: list[str], ids: list[str]) -> str:
    indices = {name: i + 1 for i, name in enumerate(ids)}
    h = 0xCBF29CE484222325
    for name in seq:
        n = indices[name]
        for shift in (0, 8, 16, 24):
            h ^= (n >> shift) & 255
            h = (h * 0x100000001B3) & MASK64
    return f"{h:016x}"


def validate_manifest(manifest: dict) -> list[dict]:
    if set(manifest) != {"schema_version", "corpus_id", "fixture_kind", "documents"}:
        fail("unexpected manifest fields")
    if type(manifest["schema_version"]) is not int or manifest["schema_version"] != 1:
        fail("invalid manifest schema")
    if manifest["corpus_id"] != CORPUS_ID or manifest["fixture_kind"] != KIND:
        fail("wrong corpus identity")
    entries = manifest["documents"]
    known = specimens()
    if type(entries) is not list or len(entries) != len(known):
        fail("document count mismatch")
    if [doc.get("id") for doc in entries] != list(known):
        fail("document identity/order mismatch")
    for doc in entries:
        if type(doc) is not dict or set(doc) != DOCUMENT_FIELDS:
            fail("missing/unexpected document fields")
        key = doc["id"]
        if not isinstance(key, str) or not ID_RE.fullmatch(key):
            fail("invalid document id")
        source, outcome, center, tags = known[key]
        if doc["source_provenance"] != PROVENANCE or doc["reference_oracle"] != "none":
            fail("invalid generated source provenance/oracle")
        if doc["expected_status"] != outcome or doc["expected_center_rgb"] != center:
            fail("manifest semantic assertion changed")
        if doc["feature_tags"] != tags or type(doc["page_index"]) is not int or doc["page_index"] != 0:
            fail("invalid case tags/index")
        if type(doc["width_points"]) is not int or doc["width_points"] != 16 or type(doc["height_points"]) is not int or doc["height_points"] != 12:
            fail("invalid page geometry")
        if type(doc["size_bytes"]) is not int or doc["size_bytes"] != len(source):
            fail("source byte length mismatch")
        if not isinstance(doc["sha256"], str) or not HASH_RE.fullmatch(doc["sha256"]) or doc["sha256"] != hashlib.sha256(source).hexdigest():
            fail("source SHA-256 mismatch: " + key)
    return entries


def verify_files(documents: list[dict], root: Path) -> None:
    if root.is_symlink() or not root.is_dir():
        fail("missing or symlinked generated assets directory")
    ids = {doc["id"] for doc in documents}
    if {p.name for p in root.iterdir()} != {key + ".pdf" for key in ids}:
        fail("extra/missing generated PDF files")
    for doc in documents:
        p = root / (doc["id"] + ".pdf")
        if p.is_symlink() or not p.is_file() or p.stat().st_size > MAX_BYTES:
            fail("invalid original PDF file type/size")
        data = p.read_bytes()
        if (len(data) != doc["size_bytes"] or
                hashlib.sha256(data).hexdigest() != doc["sha256"] or
                not data.startswith(b"%PDF-1.7\n") or
                not data.endswith(b"%%EOF\n")):
            fail("original PDF bytes changed: " + doc["id"])


def prepare(manifest: dict, root: Path) -> None:
    docs = validate_manifest(manifest)
    if root.is_symlink() or (root.exists() and not root.is_dir()):
        fail("unsafe output root")
    root.mkdir(parents=True, exist_ok=True)
    # Existing files MUST match; never silently fix tampering by rewrite.
    for doc in docs:
        path = root / (doc["id"] + ".pdf")
        data = specimens()[doc["id"]][0]
        if path.exists() or path.is_symlink():
            if path.is_symlink() or not path.is_file() or path.read_bytes() != data:
                fail("refuse changed existing generated fixture: " + doc["id"])
        else:
            path.write_bytes(data)
    verify_files(docs, root)


def validate_report(report: dict, docs: list[dict]) -> None:
    if set(report) != {"schema_version", "corpus_id", "fixture_kind", "result",
                       "document_count", "dpi", "execution_count", "positive_count",
                       "refusal_count", "pass_results", "documents"}:
        fail("invalid replay report top-level keys")
    if (type(report["schema_version"]) is not int or report["schema_version"] != 1 or
            report["corpus_id"] != CORPUS_ID or report["fixture_kind"] != KIND or
            report["result"] != "passed" or
            type(report["document_count"]) is not int or report["document_count"] != len(docs) or
            report["dpi"] != [72, 96, 144]):
        fail("invalid replay identity/dpi")
    positives = sum(d["expected_status"] == "OK" for d in docs)
    total = len(docs)*3*3
    expected_positive = positives*3*3
    expected_refusal = total-expected_positive
    for field, expected in (("execution_count", total),
                            ("positive_count", expected_positive),
                            ("refusal_count", expected_refusal)):
        if type(report[field]) is not int or report[field] != expected:
            fail("replay count mismatch: " + field)
    ids = [d["id"] for d in docs]
    perms = [ids, order_shuffle(ids, SEEDS[0]), order_shuffle(ids, SEEDS[1])]
    passes = report["pass_results"]
    if type(passes) is not list or len(passes) != 3:
        fail("missing replay passes")
    for i, (row, expected_order) in enumerate(zip(passes, perms)):
        if type(row) is not dict or set(row) != {"name", "seed", "fingerprint_fnv64",
                                                 "order", "executions", "positive", "refusals"}:
            fail("replay pass fields mismatch")
        expected_name = ("canonical", "shuffled-a", "shuffled-b")[i]
        expected_seed = ("none", "0x4cf5ad11", "0x9e3779b9")[i]
        if (row["name"] != expected_name or row["seed"] != expected_seed or
                row["order"] != expected_order or
                row["fingerprint_fnv64"] != order_fingerprint(expected_order, ids) or
                row["fingerprint_fnv64"] != EXPECTED_FINGERPRINTS[i]):
            fail("replay permutation/seed/fingerprint mismatch")
        for key, expected in (("executions", len(docs)*3),
                              ("positive", positives*3),
                              ("refusals", (len(docs)-positives)*3)):
            if type(row[key]) is not int or row[key] != expected:
                fail("replay pass count mismatch")
    observed = report["documents"]
    if type(observed) is not list or len(observed) != len(docs):
        fail("missing per-document replay evidence")
    for row, doc in zip(observed, docs):
        if type(row) is not dict or set(row) != {"id", "observed_status",
                                               "executions", "pixel_checks",
                                               "repeat_identical"}:
            fail("per-document evidence fields mismatch")
        if (row["id"] != doc["id"] or row["observed_status"] != doc["expected_status"] or
                type(row["executions"]) is not int or row["executions"] != 9 or
                type(row["pixel_checks"]) is not int or
                row["pixel_checks"] != (9 if doc["expected_status"] == "OK" else 0) or
                row["repeat_identical"] is not True):
            fail("per-document reported result mismatch: " + doc["id"])


def negative_self_tests(docs: list[dict]) -> None:
    ids = [d["id"] for d in docs]
    names = ("canonical", "shuffled-a", "shuffled-b")
    seeds = ("none", "0x4cf5ad11", "0x9e3779b9")
    perms = [ids, order_shuffle(ids, SEEDS[0]), order_shuffle(ids, SEEDS[1])]
    rows = [
        {"name": name, "seed": seed, "fingerprint_fnv64": order_fingerprint(perm, ids),
         "order": perm, "executions": len(ids)*3, "positive": 18, "refusals": 18}
        for name, seed, perm in zip(names, seeds, perms)
    ]
    report = {
        "schema_version": 1, "corpus_id": CORPUS_ID, "fixture_kind": KIND,
        "result": "passed", "document_count": len(docs), "dpi": [72, 96, 144],
        "execution_count": 108, "positive_count": 54, "refusal_count": 54,
        "pass_results": rows,
        "documents": [{"id": d["id"], "observed_status": d["expected_status"],
                       "executions": 9, "pixel_checks": (9 if d["expected_status"] == "OK" else 0),
                       "repeat_identical": True} for d in docs]
    }
    validate_report(report, docs)
    edits = (
        lambda r: r["documents"][0].update(observed_status="UNSUPPORTED"),
        lambda r: r["pass_results"][1]["order"].reverse(),
        lambda r: r["pass_results"][2].update(seed="0x00000001"),
        lambda r: r["documents"][3].update(executions=8),
        lambda r: r["pass_results"][0].update(fingerprint_fnv64="0"*16),
        lambda r: r.update(positive_count=53),
        lambda r: r["documents"].pop(),
        lambda r: r["pass_results"].pop(),
    )
    for idx, edit in enumerate(edits):
        altered = copy.deepcopy(report)
        edit(altered)
        try:
            validate_report(altered, docs)
        except ValueError:
            continue
        fail("corrupted receipt falsely accepted #" + str(idx))
    try:
        json.loads('{"id":1,"id":2}', object_pairs_hook=unique_pairs)
    except ValueError:
        pass
    else:
        fail("duplicate JSON keys accepted")


def main() -> int:
    if not __debug__:
        print("native Source corpus refuses Python -O", file=sys.stderr)
        return 1
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--mode", choices=("prepare", "reconcile"), required=True)
    args = parser.parse_args()
    try:
        manifest = read_json(args.manifest, 65536)
        if args.mode == "prepare":
            prepare(manifest, args.assets)
            if args.report is not None:
                if args.report.is_symlink():
                    fail("symlinked previous replay receipt")
                if args.report.exists():
                    if not args.report.is_file():
                        fail("non-file previous replay receipt")
                    args.report.unlink()
        else:
            docs = validate_manifest(manifest)
            verify_files(docs, args.assets)
            if args.report is None:
                fail("missing replay report")
            report = read_json(args.report, 65536)
            negative_self_tests(docs)
            validate_report(report, docs)
    except (ValueError, OSError, UnicodeError, json.JSONDecodeError) as error:
        print("[native-v10-cross-document] FAIL: " + str(error), file=sys.stderr)
        return 1
    if args.mode == "prepare":
        print(f"[native-v10-cross-document] PASS sha256/size + source identity "
              f"for {len(manifest['documents'])} generated originals")
    else:
        print("[native-v10-cross-document] PASS exact 108 executions "
              "(54 positive, 54 atomic negative), three seeds/slots, "
              "cross-document identity and corruption refusals")
    return 0


if __name__ == "__main__":
    sys.exit(main())
