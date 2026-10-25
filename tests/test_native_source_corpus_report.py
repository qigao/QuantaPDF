#!/usr/bin/env python3
"""Validate *actual* generated-source PDF qualification, including replays.

This is test-only: it never opens or re-serializes an external PDF. No run may
claim additional licensed originals from repeat executions of generated cases.
"""
import argparse
import json
import re
import sys
from pathlib import Path

REGISTRY_ROW = re.compile(
    r'^QP_SOURCE_CORPUS_FAMILY\("([^"]+)", "([^"]+)", '
    r'"([^"]+)", "([^"]+)", (\d+)u, [a-zA-Z_][a-zA-Z0-9_]*\)$'
)
SEEDS = (0x4CF5AD11, 0x9E3779B9)
EXPECTED_FAMILIES = 21
EXPECTED_FIXTURES = 71


def require(value: bool, why: str) -> None:
    if not value:
        raise ValueError(why)


def read_registry(filename: Path) -> list[dict]:
    rows = []
    for line in filename.read_text(encoding="utf-8").splitlines():
        if not line.startswith("QP_SOURCE_CORPUS_FAMILY("):
            continue
        match = REGISTRY_ROW.fullmatch(line.strip())
        require(match is not None, "invalid source registry entry: " + line)
        id_, tags, expected, oracle, count = match.groups()
        rows.append({
            "id": id_, "feature_tags": tags, "expected_outcome": expected,
            "oracle": oracle, "declared_fixtures": int(count),
        })
    require(len(rows) == EXPECTED_FAMILIES, "unexpected registry family count")
    require(len({r["id"] for r in rows}) == len(rows), "duplicate family ID")
    require(sum(r["declared_fixtures"] for r in rows) == EXPECTED_FIXTURES,
            "unexpected generated fixture total")
    return rows


def shuffled(ids: list[str], seed: int) -> list[str]:
    items = ids.copy()
    state = seed
    require(state != 0, "zero xorshift state")
    for i in range(len(items), 1, -1):
        state = (state ^ ((state << 13) & 0xFFFFFFFF)) & 0xFFFFFFFF
        state = (state ^ (state >> 17)) & 0xFFFFFFFF
        state = (state ^ ((state << 5) & 0xFFFFFFFF)) & 0xFFFFFFFF
        j = state % i
        items[i - 1], items[j] = items[j], items[i - 1]
    return items


def validate(report: dict, registry: list[dict]) -> None:
    require(type(report) is dict, "report is not a JSON object")
    require(report.get("schema_version") == 2, "wrong report schema version")
    require(report.get("corpus_id") == "native-source-pdf", "wrong corpus")
    require(report.get("fixture_kind") == "generated", "not generated source")
    require(report.get("result") == "passed", "qualification did not pass")
    require(report.get("family_count") == EXPECTED_FAMILIES,
            "report family count mismatch")
    require(report.get("fixture_count") == EXPECTED_FIXTURES,
            "report canonical fixture count mismatch")
    families = report.get("family_results")
    require(type(families) is list and len(families) == len(registry),
            "report family rows missing/mismatched")
    for idx, (actual, expected) in enumerate(zip(families, registry)):
        require(type(actual) is dict, f"family {idx} invalid")
        for key, value in expected.items():
            require(actual.get(key) == value, f"family {idx} {key} mismatched")
        require(actual.get("executed_fixtures") == expected["declared_fixtures"],
                f"family {idx} did not execute expected fixtures")
        require(actual.get("result") == "passed", f"family {idx} not passed")
    order = report.get("order_validation")
    require(type(order) is dict, "missing order qualification")
    require(order.get("algorithm") == "xorshift32-fisher-yates-v1",
            "unsupported shuffle algorithm")
    require(order.get("replay_passes") == len(SEEDS), "replay count mismatch")
    require(order.get("replay_fixture_executions") ==
            len(SEEDS) * EXPECTED_FIXTURES, "incorrect replay coverage")
    require(order.get("total_fixture_executions") ==
            (len(SEEDS) + 1) * EXPECTED_FIXTURES,
            "incorrect total execution count")
    passes = order.get("passes")
    require(type(passes) is list and len(passes) == 3, "missing order passes")
    ids = [row["id"] for row in registry]
    expected_orders = [
        ("canonical", None, ids),
        ("shuffled-a", "0x4cf5ad11", shuffled(ids, SEEDS[0])),
        ("shuffled-b", "0x9e3779b9", shuffled(ids, SEEDS[1])),
    ]
    require(expected_orders[0][2] != expected_orders[1][2] and
            expected_orders[0][2] != expected_orders[2][2] and
            expected_orders[1][2] != expected_orders[2][2],
            "shuffle orders are not distinct")
    for index, (actual, expected) in enumerate(zip(passes, expected_orders)):
        name, seed, sequence = expected
        require(type(actual) is dict, f"order {index} invalid")
        require(actual.get("name") == name and actual.get("seed") == seed,
                f"order {index} seed/name mismatch")
        require(actual.get("executed_fixtures") == EXPECTED_FIXTURES,
                f"order {index} fixture accounting mismatch")
        require(actual.get("family_order") == sequence,
                f"order {index} is not the specified reproducible permutation")
        require(len(set(actual["family_order"])) == EXPECTED_FAMILIES,
                f"order {index} skipped or duplicated families")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", required=True, type=Path)
    ap.add_argument("--registry", required=True, type=Path)
    args = ap.parse_args()
    try:
        registry = read_registry(args.registry)
        result = json.loads(args.report.read_text(encoding="utf-8"))
        validate(result, registry)
    except (ValueError, OSError, json.JSONDecodeError) as error:
        print(f"[source-corpus-order] FAIL: {error}", file=sys.stderr)
        return 1
    print(f"[source-corpus-order] PASS families={EXPECTED_FAMILIES} "
          f"canonical={EXPECTED_FIXTURES} replays={2 * EXPECTED_FIXTURES} "
          f"total={3 * EXPECTED_FIXTURES} "
          "seeds=0x4cf5ad11,0x9e3779b9")
    return 0


if __name__ == "__main__":
    sys.exit(main())
