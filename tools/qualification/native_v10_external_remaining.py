#!/usr/bin/env python3
"""Reconcile unchanged original yWorks and TeXample PUBLIC C11 RGB.

The existing verifier checks all seven source hashes, licenses and byte lengths.
This separate gate requires real public yWorks 72-DPI and TeXample 72/96/144-DPI RGB across independent owners,
the frozen original-decimal TeXample reference, unchanged yWorks higher-DPI atomic refusals, and
retained bitmaps after Page, Document and Context destruction. Historical corpus
metadata and synthetic schema controls never count as native rendering evidence.
"""
from __future__ import annotations

import argparse
import copy
import json
import hashlib
from pathlib import Path
import sys

from external_pdf_corpus import verify_corpus, CorpusAdmissionError

IDS = (
    "yworks.clippath-cliprule-v1",
    "texample.intersecting-rings-v1",
)
PRIOR_OUTCOMES = (
    "supported-wstar-clip",
    "supported-wstar-clip",
)
BLANK = "tinytoolkit.sample-blank-letter-v1"
SEEDS = (0x4CF5AD11, 0x9E3779B9)
FINGERPRINTS = (
    "c9c28939c99668c6",
    "46c2da3be7c31176",
    "c9c28939c99668c6",
)
TEXAMPLE_RGBA_SHA256 = "51e36fcf3a39e93724e1a3176286944883378df9b39c942147d52827607f957e"
DPI_REFERENCES = [
    {"dpi": 96, "width": 317, "height": 241,
     "sha256": "b5ed960392f23222c4f90f6e40a1aecd370cb71b99e50a8870b45fa2a1089d4a"},
    {"dpi": 144, "width": 475, "height": 361,
     "sha256": "19c7350ffc8e41d073c6172e421a535826735c171389c33cf84034f0b82c1edc"},
]
MASK32 = (1 << 32) - 1
MASK64 = (1 << 64) - 1


def require(value: bool, message: str) -> None:
    if not value:
        raise ValueError(message)


def exact_int(value: object, expected: int, why: str) -> None:
    require(type(value) is int and value == expected, why)


def unique_pairs(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def reject_constant(token: str):
    raise ValueError("nonfinite JSON: " + token)


def read_json(path: Path):
    require(path.is_file() and not path.is_symlink() and
            0 < path.stat().st_size <= 262144,
            "missing/unsafe/oversize source evidence: " + str(path))
    return json.loads(path.read_bytes().decode("utf-8"),
                      object_pairs_hook=unique_pairs,
                      parse_constant=reject_constant)


def shuffled(ids: list[str], seed: int) -> list[str]:
    state = seed
    order = ids.copy()
    for n in range(len(order), 1, -1):
        state = (state ^ ((state << 13) & MASK32)) & MASK32
        state = (state ^ (state >> 17)) & MASK32
        state = (state ^ ((state << 5) & MASK32)) & MASK32
        index = state % n
        order[n - 1], order[index] = order[index], order[n - 1]
    return order


def fingerprint(order: list[str]) -> str:
    labels = {name: i + 1 for i, name in enumerate(IDS)}
    value = 0xCBF29CE484222325
    for name in order:
        require(name in labels, "unknown source in replay order")
        index = labels[name]
        for shift in (0, 8, 16, 24):
            value ^= (index >> shift) & 255
            value = (value * 0x100000001B3) & MASK64
    return f"{value:016x}"


def validate_integrity(report: dict) -> None:
    require(report["schema_version"] == 1 and
            report["corpus_id"] == "external-source-pdf" and
            report["fixture_kind"] == "external" and
            report["result"] == "integrity_verified_only",
            "external integrity record identity")
    exact_int(report["document_count"], 7, "original count")
    exact_int(report["native_conformance_passed_count"], 0,
              "integrity-only admission must not count native success")
    rows = report["documents"]
    require(type(rows) is list and len(rows) == 7, "external row count")
    indexed = {row["id"]: row for row in rows}
    require(len(indexed) == 7, "duplicate source IDs")
    for name, expected in zip(IDS, PRIOR_OUTCOMES):
        record = indexed.get(name)
        require(record is not None and record["expected_native_outcome"] == expected
                and record["integrity"] == "verified" and
                record["native_conformance"] == "not_run",
                "missing original integrity or historical profile: " + name)
    require(indexed[BLANK]["expected_native_outcome"] == "supported-blank-page",
            "blank external reference missing")


def validate_native(report: dict) -> None:
    require(type(report) is dict and set(report) == {
        "schema_version", "corpus_id", "qualification", "reference_oracle",
        "result", "manifest_document_count", "negative_document_count",
        "recovery_document_id", "dpi", "execution_count", "positive_count",
        "refusal_count", "passes", "documents", "recovery",
        "texample_rgba_reference_sha256", "texample_dpi_references"
    }, "unknown/absent public Native evidence fields")
    exact_int(report["schema_version"], 2, "schema_version")
    require(report["corpus_id"] == "external-source-pdf" and
            report["qualification"] == "native-v10-yworks-texample-physical-dpi" and
            report["reference_oracle"] == "none" and
            report["texample_rgba_reference_sha256"] == TEXAMPLE_RGBA_SHA256 and
            report["texample_dpi_references"] == DPI_REFERENCES and
            report["result"] == "passed" and
            report["recovery_document_id"] == BLANK,
            "invalid qualification / oracle")
    references = report["texample_dpi_references"]
    require(type(references) is list and len(references) == 2, "DPI reference count")
    for given, expected in zip(references, DPI_REFERENCES):
        require(type(given) is dict and set(given) == set(expected), "DPI reference fields")
        for key in ("dpi", "width", "height"):
            exact_int(given[key], expected[key], "DPI reference " + key)
    for key, value in (
        ("manifest_document_count", 7),
        ("negative_document_count", 0),
        ("dpi", 72),
        ("execution_count", 24),
        ("positive_count", 18),
        ("refusal_count", 6),
    ):
        exact_int(report[key], value, key)

    ids = list(IDS)
    expected_orders = (
        ids, shuffled(ids, SEEDS[0]), shuffled(ids, SEEDS[1])
    )
    require(len({tuple(row) for row in expected_orders}) == 2,
            "shuffle orders collide")
    passes = report["passes"]
    require(type(passes) is list and len(passes) == 3, "replay pass count")
    for i, (given, expected) in enumerate(zip(passes, expected_orders)):
        require(type(given) is dict and set(given) == {
            "name", "seed", "fingerprint_fnv64", "ids",
            "refused", "accepted_rgb", "recovered"
        }, "unknown/missing pass fields")
        require(given["name"] == ("canonical", "shuffled-a", "shuffled-b")[i]
                and given["seed"] == ("none", "0x4cf5ad11", "0x9e3779b9")[i]
                and type(given["ids"]) is list and given["ids"] == expected
                and given["fingerprint_fnv64"] == fingerprint(expected)
                and given["fingerprint_fnv64"] == FINGERPRINTS[i],
                "invalid replay seed/order/fingerprint")
        exact_int(given["refused"], 2, "per-pass actual DPI refusals")
        exact_int(given["accepted_rgb"], 4, "per-pass exact original RGB")
        exact_int(given["recovered"], 2, "per-pass blank recovery")

    rows = report["documents"]
    require(type(rows) is list and len(rows) == 2, "per-document result count")
    for index, (row, name) in enumerate(zip(rows, IDS)):
        require(type(row) is dict and set(row) == {
            "id", "observed_status", "executions",
            "exact_rgb_pixels", "atomic_null_bitmap", "unsupported_dpi_executions",
            "unsupported_dpi_atomic_null_bitmap", "bitmap_survived_context",
            "qualified_dpis", "exact_rgb_executions"
        }, "invalid per-document fields")
        require(row["id"] == name and row["observed_status"] == "OK"
                and row["exact_rgb_pixels"] is True
                and row["atomic_null_bitmap"] is False
                and row["unsupported_dpi_atomic_null_bitmap"] is (index == 0)
                and row["bitmap_survived_context"] is True,
                "wrong authentic original RGB or owner lifetime for " + name)
        exact_int(row["executions"], 3, "per-document calls")
        exact_int(row["unsupported_dpi_executions"], 6 if index == 0 else 0,
                  "per-document DPI refusals")
        exact_int(row["exact_rgb_executions"], 3 if index == 0 else 9,
                  "per-document actual exact RGB calls")
        require(type(row["qualified_dpis"]) is list and
                all(type(dpi) is int for dpi in row["qualified_dpis"]) and
                row["qualified_dpis"] == ([72] if index == 0 else [72, 96, 144]),
                "missing/forged physical DPI qualification")
    blank = report["recovery"]
    require(type(blank) is dict and set(blank) == {
        "id", "observed_status", "executions",
        "all_white", "bitmap_survived_context"
    }, "invalid recovery fields")
    require(blank["id"] == BLANK and blank["observed_status"] == "OK"
            and blank["all_white"] is True
            and blank["bitmap_survived_context"] is True,
            "missing true native blank recovery")
    exact_int(blank["executions"], 6, "recovery executions")


def synthetic_report() -> dict:
    ids = list(IDS)
    orders = (ids, shuffled(ids, SEEDS[0]), shuffled(ids, SEEDS[1]))
    return {
        "schema_version": 2, "corpus_id": "external-source-pdf",
        "qualification": "native-v10-yworks-texample-physical-dpi",
        "reference_oracle": "none", "result": "passed",
        "texample_rgba_reference_sha256": TEXAMPLE_RGBA_SHA256,
        "texample_dpi_references": copy.deepcopy(DPI_REFERENCES),
        "manifest_document_count": 7, "negative_document_count": 0,
        "recovery_document_id": BLANK, "dpi": 72,
        "execution_count": 24, "positive_count": 18,
        "refusal_count": 6,
        "passes": [
            {"name": ("canonical", "shuffled-a", "shuffled-b")[i],
             "seed": ("none", "0x4cf5ad11", "0x9e3779b9")[i],
             "fingerprint_fnv64": fingerprint(order), "ids": order,
             "refused": 2, "accepted_rgb": 4, "recovered": 2}
            for i, order in enumerate(orders)
        ],
        "documents": [
            {"id": id_, "observed_status": "OK",
             "executions": 3, "exact_rgb_pixels": True,
             "atomic_null_bitmap": False, "unsupported_dpi_executions": 6 if i == 0 else 0,
             "unsupported_dpi_atomic_null_bitmap": i == 0,
             "qualified_dpis": [72] if i == 0 else [72, 96, 144],
             "exact_rgb_executions": 3 if i == 0 else 9,
             "bitmap_survived_context": True} for i, id_ in enumerate(IDS)
        ],
        "recovery": {
            "id": BLANK, "observed_status": "OK", "executions": 6,
            "all_white": True, "bitmap_survived_context": True
        }
    }


def negative_self_tests() -> None:
    valid = synthetic_report()
    validate_native(valid)
    edits = (
        lambda r: r.update(reference_oracle="original-pdfium"),
        lambda r: r.update(positive_count=10),
        lambda r: r["documents"][1].update(observed_status="FORMAT"),
        lambda r: r["documents"][0].update(atomic_null_bitmap=True),
        lambda r: r["documents"][0].update(exact_rgb_pixels=False),
        lambda r: r["documents"][1].update(observed_status="UNSUPPORTED"),
        lambda r: r.update(qualification="native-v10-yworks-rgb-texample-refused"),
        lambda r: r.update(execution_count=12, positive_count=9, refusal_count=3),
        lambda r: r.update(texample_rgba_reference_sha256="0" * 64),
        lambda r: r["documents"][1].update(unsupported_dpi_executions=6),
        lambda r: r["documents"][0].update(unsupported_dpi_atomic_null_bitmap=False),
        lambda r: r["documents"][1].update(bitmap_survived_context=False),
        lambda r: r["passes"][0].update(accepted_rgb=0),
        lambda r: r["passes"][1].update(seed="0xdeadbeef"),
        lambda r: r["passes"][2]["ids"].reverse(),
        lambda r: r["passes"][0].update(fingerprint_fnv64="0" * 16),
        lambda r: r["recovery"].update(bitmap_survived_context=False),
        lambda r: r["documents"][1].update(executions=True),
        lambda r: r["documents"].pop(),
        lambda r: r.update(schema_version=1),
        lambda r: r.update(positive_count=12, refusal_count=12),
        lambda r: r["documents"][1].update(qualified_dpis=[72]),
        lambda r: r["documents"][1].update(exact_rgb_executions=3),
        lambda r: r["texample_dpi_references"][0].update(sha256="0" * 64),
        lambda r: r["texample_dpi_references"][1].update(width=474),
        lambda r: r["texample_dpi_references"][0].update(width=317.0),
        lambda r: r["documents"][1].update(qualified_dpis=[72, 96.0, 144]),
    )
    for i, change in enumerate(edits):
        corrupted = copy.deepcopy(valid)
        change(corrupted)
        try:
            validate_native(corrupted)
        except ValueError:
            continue
        raise ValueError(f"corrupted native evidence #{i} was accepted")
    try:
        json.loads('{"id":1,"id":2}', object_pairs_hook=unique_pairs)
    except ValueError:
        pass
    else:
        raise ValueError("duplicate JSON key accepted")


def main() -> int:
    if not __debug__:
        print("Native external remainder reconciliation refuses Python -O",
              file=sys.stderr)
        return 1
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manifest", required=True, type=Path)
    ap.add_argument("--assets", required=True, type=Path)
    ap.add_argument("--integrity", required=True, type=Path)
    ap.add_argument("--receipt", required=True, type=Path)
    args = ap.parse_args()
    try:
        verified = verify_corpus(args.manifest, args.assets)
        require(read_json(args.integrity) == verified,
                "stale/mismatched external integrity-only report")
        validate_integrity(verified)
        negative_self_tests()
        reference = args.assets.parent.parent / "native" / "goldens" / "texample72-m4b.rgba"
        require(reference.is_file() and not reference.is_symlink() and
                reference.stat().st_size == 172312, "missing/unsafe original-decimal reference")
        require(hashlib.sha256(reference.read_bytes()).hexdigest() == TEXAMPLE_RGBA_SHA256,
                "original-decimal reference hash changed")
        for record in DPI_REFERENCES:
            higher = reference.parent / f"texample{record['dpi']}-m4c.rgba"
            require(higher.is_file() and not higher.is_symlink() and
                    higher.stat().st_size == record["width"] * record["height"] * 4 and
                    hashlib.sha256(higher.read_bytes()).hexdigest() == record["sha256"],
                    "missing/unsafe/changed original high-DPI reference")
        validate_native(read_json(args.receipt))
    except (ValueError, OSError, UnicodeError, KeyError, TypeError,
            json.JSONDecodeError, CorpusAdmissionError) as exc:
        print("[native-external-remainder] FAIL: " + str(exc),
              file=sys.stderr)
        return 1
    print("[native-external-remainder] PASS 7 SHA-reviewed originals; "
          "unchanged yWorks 72-DPI RGB x3; full TeXample 72/96/144-DPI RGB x3 each; "
          "yWorks DPI refusals x6; blank recovery x6")
    return 0


if __name__ == "__main__":
    sys.exit(main())
