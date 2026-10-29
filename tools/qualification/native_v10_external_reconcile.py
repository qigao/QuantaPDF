#!/usr/bin/env python3
"""Reconcile real Native V10 semantic evidence with the existing external corpus.

Never count the external integrity-only receipt as native conformance. Every
external original is independently rehashed against the existing manifest,
including its declared source/license review, BEFORE trusting the separate
public C11 replay receipt. No PDFium/reference renderer is executed here.
"""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import re
import sys

from external_pdf_corpus import CorpusAdmissionError, verify_corpus

MAX_JSON_BYTES = 256 * 1024
SEEDS = (0x4CF5AD11, 0x9E3779B9)
FINGERPRINTS = ("fd1f0f4381eb0395", "abb05270b835c595",
                "7ff945097187cf45")
IDS = (
    "tinytoolkit.sample-blank-letter-v1",
    "tinytoolkit.sample-1-page-v1",
    "pdfium.wstar-late-text-v1",
)
STATUSES = ("OK", "UNSUPPORTED", "UNSUPPORTED")
OUTCOMES = ("supported-blank-page", "unsupported", "unsupported")
MASK32 = (1 << 32) - 1
MASK64 = (1 << 64) - 1
ROOT_KEYS = {
    "schema_version", "corpus_id", "fixture_kind", "qualification",
    "reference_oracle", "result", "manifest_document_count",
    "qualified_document_count", "execution_count", "positive_count",
    "refusal_count", "dpi", "passes", "documents"
}


def require(condition: bool, why: str) -> None:
    if not condition:
        raise ValueError(why)


def unique_pairs(pairs: list[tuple[str, object]]) -> dict:
    out: dict = {}
    for key, value in pairs:
        require(key not in out, "duplicate JSON key: " + key)
        out[key] = value
    return out


def reject_nonfinite(token: str):
    raise ValueError("nonfinite JSON token: " + token)


def read_json(path: Path) -> dict:
    if not path.is_file() or path.is_symlink() or not 0 < path.stat().st_size <= MAX_JSON_BYTES:
        raise ValueError("missing, symlinked or excessive evidence: " + str(path))
    report = json.loads(path.read_bytes().decode("utf-8"),
                        object_pairs_hook=unique_pairs,
                        parse_constant=reject_nonfinite)
    require(type(report) is dict, "JSON root is not an object")
    return report


def shuffle(ids: list[str], seed: int) -> list[str]:
    out = ids.copy()
    require(seed != 0, "zero shuffle seed")
    state = seed
    for n in range(len(out), 1, -1):
        state ^= (state << 13) & MASK32
        state ^= state >> 17
        state ^= (state << 5) & MASK32
        state &= MASK32
        index = state % n
        out[n - 1], out[index] = out[index], out[n - 1]
    return out


def fingerprint(seq: list[str]) -> str:
    mapping = {key: index + 1 for index, key in enumerate(IDS)}
    require(len(mapping) == len(IDS), "duplicate anchored ID")
    h = 0xCBF29CE484222325
    for name in seq:
        require(name in mapping, "unknown original PDF")
        value = mapping[name]
        for shift in (0, 8, 16, 24):
            h ^= (value >> shift) & 255
            h = (h * 0x100000001B3) & MASK64
    return f"{h:016x}"


def exact_int(x, expected: int, why: str) -> None:
    require(type(x) is int and x == expected, why)


def qualified_records(integrity: dict) -> None:
    require(integrity["schema_version"] == 1 and
            integrity["corpus_id"] == "external-source-pdf" and
            integrity["fixture_kind"] == "external" and
            integrity["result"] == "integrity_verified_only", "wrong external identity")
    exact_int(integrity["native_conformance_passed_count"], 0,
              "integrity-only evidence claiming Native support")
    exact_int(integrity["document_count"], 7, "unexpected external document count")
    indexed = {d["id"]: d for d in integrity["documents"]}
    require(len(indexed) == 7 and len(integrity["documents"]) == 7,
            "duplicate external documents")
    for name, outcome in zip(IDS, OUTCOMES):
        require(name in indexed and indexed[name]["expected_native_outcome"] == outcome
                and indexed[name]["integrity"] == "verified" and
                indexed[name]["native_conformance"] == "not_run",
                "unqualified original: " + name)
        require(type(indexed[name]["sha256"]) is str and
                re.fullmatch(r"[0-9a-f]{64}", indexed[name]["sha256"]) is not None
                and type(indexed[name]["size_bytes"]) is int,
                "original source SHA/size evidence is absent")


def validate_receipt(receipt: dict) -> None:
    require(set(receipt) == ROOT_KEYS, "missing/unexpected Native receipt fields")
    exact_int(receipt["schema_version"], 1, "schema_version")
    require(receipt["corpus_id"] == "external-source-pdf" and
            receipt["fixture_kind"] == "external" and
            receipt["qualification"] == "native-v10-public" and
            receipt["reference_oracle"] == "none" and
            receipt["result"] == "passed", "wrong qualification/oracle identity")
    for key, value in (("manifest_document_count", 7),
                       ("qualified_document_count", 3),
                       ("execution_count", 9),
                       ("positive_count", 3),
                       ("refusal_count", 6),
                       ("dpi", 72)):
        exact_int(receipt[key], value, key)
    ids = list(IDS)
    orders = (ids, shuffle(ids, SEEDS[0]), shuffle(ids, SEEDS[1]))
    labels = ("canonical", "shuffled-a", "shuffled-b")
    seed_labels = ("none", "0x4cf5ad11", "0x9e3779b9")
    require(type(receipt["passes"]) is list and len(receipt["passes"]) == 3,
            "replay pass count")
    require(len({tuple(order) for order in orders}) == 3,
            "replay shuffles not distinct")
    for i, (given, order) in enumerate(zip(receipt["passes"], orders)):
        require(type(given) is dict and set(given) == {
            "name", "seed", "fingerprint_fnv64", "ids",
            "executions", "positive", "refusals"
        }, "replay pass fields")
        require(given["name"] == labels[i] and given["seed"] == seed_labels[i]
                and type(given["ids"]) is list and given["ids"] == order and
                type(given["fingerprint_fnv64"]) is str and
                given["fingerprint_fnv64"] == fingerprint(order) and
                given["fingerprint_fnv64"] == FINGERPRINTS[i],
                "replay seed, order or fingerprint mismatch")
        for key, val in (("executions", 3), ("positive", 1), ("refusals", 2)):
            exact_int(given[key], val, "replay per-pass " + key)

    docs = receipt["documents"]
    require(type(docs) is list and len(docs) == 3, "wrong qualified document rows")
    for i, actual in enumerate(docs):
        require(type(actual) is dict and set(actual) == {
            "id", "observed_status", "executions", "exact_blank_pixels",
            "atomic_null_bitmap"
        }, "wrong per-document result fields")
        require(actual["id"] == IDS[i] and
                actual["observed_status"] == STATUSES[i],
                "wrong original or actual Native status")
        exact_int(actual["executions"], 3, "per-original executions")
        require(actual["exact_blank_pixels"] is (i == 0) and
                actual["atomic_null_bitmap"] is (i != 0),
                "original blank/atomic-refusal proof mismatch")


def synthetic_receipt() -> dict:
    ids = list(IDS)
    orders = (ids, shuffle(ids, SEEDS[0]), shuffle(ids, SEEDS[1]))
    return {
        "schema_version": 1, "corpus_id": "external-source-pdf",
        "fixture_kind": "external", "qualification": "native-v10-public",
        "reference_oracle": "none", "result": "passed",
        "manifest_document_count": 7, "qualified_document_count": 3,
        "execution_count": 9, "positive_count": 3, "refusal_count": 6, "dpi": 72,
        "passes": [
            {"name": name, "seed": seed, "fingerprint_fnv64": fingerprint(order),
             "ids": order, "executions": 3, "positive": 1, "refusals": 2}
            for name, seed, order in zip(
                ("canonical", "shuffled-a", "shuffled-b"),
                ("none", "0x4cf5ad11", "0x9e3779b9"), orders)
        ],
        "documents": [
            {"id": name, "observed_status": status,
             "executions": 3, "exact_blank_pixels": i == 0,
             "atomic_null_bitmap": i != 0}
            for i, (name, status) in enumerate(zip(IDS, STATUSES))
        ],
    }


def corruption_self_tests() -> None:
    baseline = synthetic_receipt()
    validate_receipt(baseline)
    corruptions = (
        lambda r: r.update(qualified_document_count=7),
        lambda r: r.update(reference_oracle="original-pdfium"),
        lambda r: r["passes"][1].update(seed="0x00000001"),
        lambda r: r["passes"][2]["ids"].reverse(),
        lambda r: r["passes"][0].update(fingerprint_fnv64="0" * 16),
        lambda r: r["documents"][1].update(observed_status="OK"),
        lambda r: r["documents"][0].update(exact_blank_pixels=False),
        lambda r: r.update(positive_count=4),
        lambda r: r["documents"][0].update(executions=True),
        lambda r: r["documents"].pop(),
    )
    for i, mutation in enumerate(corruptions):
        altered = copy.deepcopy(baseline)
        mutation(altered)
        try:
            validate_receipt(altered)
        except ValueError:
            continue
        require(False, f"corruption control {i} was accepted")
    try:
        json.loads('{"status":1,"status":2}', object_pairs_hook=unique_pairs)
    except ValueError:
        pass
    else:
        require(False, "duplicate JSON key accepted")


def main() -> int:
    if not __debug__:
        print("external Native V10 reconciliation refuses Python -O", file=sys.stderr)
        return 1
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manifest", required=True, type=Path)
    ap.add_argument("--assets", required=True, type=Path)
    ap.add_argument("--integrity", required=True, type=Path)
    ap.add_argument("--receipt", required=True, type=Path)
    args = ap.parse_args()
    try:
        # The canonical *existing* verifier checks all seven origins,
        # licenses, byte lengths, hashes and absence of rogue PDF assets.
        newly_verified = verify_corpus(args.manifest, args.assets)
        integrity = read_json(args.integrity)
        require(integrity == newly_verified,
                "old or forged external integrity-only report")
        qualified_records(newly_verified)
        corruption_self_tests()
        receipt = read_json(args.receipt)
        validate_receipt(receipt)
    except (ValueError, OSError, UnicodeError, json.JSONDecodeError,
            CorpusAdmissionError, KeyError, TypeError) as error:
        print("[native-v10-external] FAIL: " + str(error), file=sys.stderr)
        return 1
    print("[native-v10-external] PASS seven SHA-pinned external originals "
          "integrity verified; three *separately* native qualified: "
          "one RGB blank, two explicit rejects, nine invocations. "
          "Four other originals NOT qualified by this gate.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
