#!/usr/bin/env python3
"""Strict offline validation of the generated source-PDF replay receipt.

Test-only: no external PDF is fetched, modified, counted or admitted here.
The report must prove 22 families, 72 distinct fixture scenarios and three
separately verified executions. Corruption self-tests guard this checker.
"""

import argparse
import copy
import json
import re
import sys
from pathlib import Path

REGISTRY_ROW = re.compile(
    r'^QP_SOURCE_CORPUS_FAMILY\("([^"]+)", "([^"]+)", '
    r'"([^"]+)", "([^"]+)", (\d+)u, [a-zA-Z_][a-zA-Z0-9_]*\)$'
)
SEEDS = (0x4CF5AD11, 0x9E3779B9)
FINGERPRINTS = ("7804171fea529e52", "685d706895285ca2",
                "b57eda0d6f4bd672")
EXPECTED_FAMILIES = 22
EXPECTED_FIXTURES = 72
MASK32 = (1 << 32) - 1
MASK64 = (1 << 64) - 1


def require(value: bool, why: str) -> None:
    if not value:
        raise ValueError(why)


def keys_exact(value, expected, why):
    require(type(value) is dict and set(value) == set(expected),
            why + ": missing or unexpected object keys")


def count_exact(value, expected, why):
    require(type(value) is int and value == expected, why + ": count mismatch")


def unique_pairs(pairs):
    """Reject ambiguous duplicate JSON keys rather than overwriting them."""
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def read_registry(filename: Path) -> list[dict]:
    rows = []
    for line in filename.read_text(encoding="utf-8").splitlines():
        if not line.startswith("QP_SOURCE_CORPUS_FAMILY("):
            continue
        match = REGISTRY_ROW.fullmatch(line.strip())
        require(match is not None, "invalid source registry entry: " + line)
        id_, tags, expected, oracle, count = match.groups()
        require(id_ and tags and expected in ("accepted", "rejected", "mixed")
                and oracle in ("original-pdfium", "native-only", "status-only"),
                "invalid registry metadata")
        rows.append({
            "id": id_, "feature_tags": tags, "expected_outcome": expected,
            "oracle": oracle, "declared_fixtures": int(count),
        })
    require(len(rows) == EXPECTED_FAMILIES, "unexpected registry family count")
    require(len({r["id"] for r in rows}) == len(rows), "duplicate family ID")
    require(all(r["declared_fixtures"] > 0 for r in rows),
            "invalid registry fixture count")
    require(sum(r["declared_fixtures"] for r in rows) == EXPECTED_FIXTURES,
            "unexpected generated fixture total")
    return rows


def shuffled(ids: list[str], seed: int) -> list[str]:
    items = ids.copy()
    state = seed
    require(state != 0, "zero xorshift state")
    for i in range(len(items), 1, -1):
        state = (state ^ ((state << 13) & MASK32)) & MASK32
        state = (state ^ (state >> 17)) & MASK32
        state = (state ^ ((state << 5) & MASK32)) & MASK32
        j = state % i
        items[i - 1], items[j] = items[j], items[i - 1]
    return items


def order_fingerprint(sequence: list[str], registry: list[dict]) -> str:
    by_id = {row["id"]: i + 1 for i, row in enumerate(registry)}
    require(len(by_id) == len(registry), "duplicate registry IDs")
    h = 0xCBF29CE484222325
    for name in sequence:
        require(name in by_id, "fingerprint refers to unknown family")
        value = by_id[name]
        for shift in (0, 8, 16, 24):
            h ^= (value >> shift) & 255
            h = (h * 0x100000001B3) & MASK64
    return f"{h:016x}"


def validate(report: dict, registry: list[dict]) -> None:
    keys_exact(report, (
        "schema_version", "corpus_id", "fixture_kind", "result",
        "family_count", "fixture_count", "family_results", "order_validation",
    ), "report")
    count_exact(report["schema_version"], 2, "schema_version")
    require(report["corpus_id"] == "native-source-pdf" and
            report["fixture_kind"] == "generated" and
            report["result"] == "passed", "wrong report identity/result")
    count_exact(report["family_count"], EXPECTED_FAMILIES, "family_count")
    count_exact(report["fixture_count"], EXPECTED_FIXTURES, "fixture_count")

    families = report["family_results"]
    require(type(families) is list and len(families) == len(registry),
            "family rows missing/mismatched")
    for index, (actual, expected) in enumerate(zip(families, registry)):
        keys_exact(actual, (
            "id", "feature_tags", "expected_outcome", "oracle",
            "declared_fixtures", "executed_fixtures", "result",
        ), f"family {index}")
        for key, value in expected.items():
            require(actual[key] == value and type(actual[key]) is type(value),
                    f"family {index} {key} mismatch")
        count_exact(actual["executed_fixtures"],
                    expected["declared_fixtures"],
                    f"family {index} observed count")
        require(actual["result"] == "passed", f"family {index} not passed")

    order = report["order_validation"]
    keys_exact(order, (
        "algorithm", "replay_passes", "replay_fixture_executions",
        "total_fixture_executions", "passes",
    ), "order_validation")
    require(order["algorithm"] == "xorshift32-fisher-yates-v1",
            "unsupported shuffle protocol")
    count_exact(order["replay_passes"], len(SEEDS), "replay_passes")
    count_exact(order["replay_fixture_executions"],
                len(SEEDS) * EXPECTED_FIXTURES, "replay_fixture_executions")
    count_exact(order["total_fixture_executions"],
                (len(SEEDS) + 1) * EXPECTED_FIXTURES,
                "total_fixture_executions")
    passes = order["passes"]
    require(type(passes) is list and len(passes) == len(FINGERPRINTS),
            "missing replay passes")
    ids = [row["id"] for row in registry]
    expected_orders = [
        ("canonical", None, ids),
        ("shuffled-a", "0x4cf5ad11", shuffled(ids, SEEDS[0])),
        ("shuffled-b", "0x9e3779b9", shuffled(ids, SEEDS[1])),
    ]
    require(len({tuple(row[2]) for row in expected_orders}) == len(FINGERPRINTS),
            "shuffle orders are not distinct")
    by_id = {row["id"]: row["declared_fixtures"] for row in registry}
    for index, (actual, expected) in enumerate(zip(passes, expected_orders)):
        name, seed, sequence = expected
        keys_exact(actual, (
            "name", "seed", "executed_fixtures", "fingerprint_fnv64",
            "family_order", "observed_fixtures",
        ), f"replay {index}")
        require(actual["name"] == name and actual["seed"] == seed,
                f"replay {index} seed/name mismatch")
        count_exact(actual["executed_fixtures"], EXPECTED_FIXTURES,
                    f"replay {index} total")
        require(type(actual["family_order"]) is list and
                actual["family_order"] == sequence and
                len(set(actual["family_order"])) == EXPECTED_FAMILIES,
                f"replay {index} permutation mismatch")
        expected_counts = [by_id[x] for x in sequence]
        require(type(actual["observed_fixtures"]) is list and
                actual["observed_fixtures"] == expected_counts and
                all(type(n) is int for n in actual["observed_fixtures"]),
                f"replay {index} family-count mismatch")
        expected_hash = order_fingerprint(sequence, registry)
        require(expected_hash == FINGERPRINTS[index] and
                actual["fingerprint_fnv64"] == expected_hash,
                f"replay {index} portable fingerprint mismatch")


def synthetic_report(registry: list[dict]) -> dict:
    ids = [row["id"] for row in registry]
    orders = (ids, shuffled(ids, SEEDS[0]), shuffled(ids, SEEDS[1]))
    names = ("canonical", "shuffled-a", "shuffled-b")
    seeds = (None, "0x4cf5ad11", "0x9e3779b9")
    by_id = {row["id"]: row["declared_fixtures"] for row in registry}
    family_results = [dict(row, executed_fixtures=row["declared_fixtures"],
                           result="passed") for row in registry]
    passes = [
        {
            "name": name, "seed": seed,
            "executed_fixtures": EXPECTED_FIXTURES,
            "fingerprint_fnv64": order_fingerprint(perm, registry),
            "family_order": perm, "observed_fixtures": [by_id[x] for x in perm],
        }
        for name, seed, perm in zip(names, seeds, orders)
    ]
    return {
        "schema_version": 2, "corpus_id": "native-source-pdf",
        "fixture_kind": "generated", "result": "passed",
        "family_count": EXPECTED_FAMILIES, "fixture_count": EXPECTED_FIXTURES,
        "family_results": family_results,
        "order_validation": {
            "algorithm": "xorshift32-fisher-yates-v1",
            "replay_passes": 2,
            "replay_fixture_executions": 2 * EXPECTED_FIXTURES,
            "total_fixture_executions": 3 * EXPECTED_FIXTURES,
            "passes": passes,
        },
    }


def negative_self_tests(registry: list[dict]) -> None:
    baseline = synthetic_report(registry)
    validate(baseline, registry)
    corruptions = (
        lambda r: r["order_validation"]["passes"][1].update(seed="0x00000001"),
        lambda r: r["order_validation"]["passes"][2]["family_order"].reverse(),
        lambda r: r["order_validation"]["passes"][0]["observed_fixtures"].pop(),
        lambda r: r["order_validation"]["passes"][1].update(
            fingerprint_fnv64="0000000000000000"),
        lambda r: r["family_results"][0].update(executed_fixtures=0),
        lambda r: r["order_validation"].update(total_fixture_executions=58),
        lambda r: r.update(schema_version=1),
        lambda r: r["order_validation"]["passes"].pop(),
    )
    for idx, mutate in enumerate(corruptions):
        modified = copy.deepcopy(baseline)
        mutate(modified)
        try:
            validate(modified, registry)
        except ValueError:
            continue
        raise ValueError(f"corrupt receipt #{idx} unexpectedly accepted")
    try:
        json.loads('{"duplicate": 1, "duplicate": 2}', object_pairs_hook=unique_pairs)
    except ValueError:
        pass
    else:
        raise ValueError("duplicate JSON keys unexpectedly accepted")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", required=True, type=Path)
    ap.add_argument("--registry", required=True, type=Path)
    args = ap.parse_args()
    try:
        registry = read_registry(args.registry)
        negative_self_tests(registry)
        result = json.loads(args.report.read_text(encoding="utf-8"),
                            object_pairs_hook=unique_pairs)
        validate(result, registry)
    except (ValueError, OSError, json.JSONDecodeError) as error:
        print(f"[source-corpus-order] FAIL: {error}", file=sys.stderr)
        return 1
    print(f"[source-corpus-order] PASS families={EXPECTED_FAMILIES} "
          f"canonical={EXPECTED_FIXTURES} replays={2 * EXPECTED_FIXTURES} "
          f"total={3 * EXPECTED_FIXTURES}; verified per-family counts, "
          "exact seeds, portable fingerprints and corruption refusals")
    return 0


if __name__ == "__main__":
    sys.exit(main())
