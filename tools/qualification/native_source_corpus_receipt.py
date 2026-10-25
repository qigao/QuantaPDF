#!/usr/bin/env python3
"""Strict, offline source-PDF generated-fixture replay receipt verifier.

Validate schema, manifest IDs/counts, all three deterministic execution
orders, per-family observations and fixed portable fingerprints. The same
validator intentionally rejects mutated synthetic reports in self-tests.
This does not admit or count external licensed original PDFs.
"""

import argparse
import copy
import json
import re
from pathlib import Path

EXPECTED_FAMILIES = 20
EXPECTED_FIXTURES = 58
SEEDS = (0, 0x21423301, 0x21423302)
FINGERPRINTS = (
    "7805220618c529f1",
    "fa1b13a80bcce7c1",
    "f69afd32dbf9e3a1",
)
MASK32 = (1 << 32) - 1
MASK64 = (1 << 64) - 1
ROW = re.compile(
    r'QP_SOURCE_CORPUS_FAMILY\("([^"]+)", "([^"]+)", '
    r'"(accepted|rejected|mixed)", "(original-pdfium|native-only|status-only)", '
    r'(\d+)u, ([A-Za-z_][A-Za-z_0-9]*)\)'
)


class InvalidReceipt(ValueError):
    """Report is incomplete, stale, ambiguous or inconsistent with manifest."""


def require(condition, message):
    if not condition:
        raise InvalidReceipt(message)


def load_manifest(path):
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line.startswith("QP_SOURCE_CORPUS_FAMILY("):
            continue
        m = ROW.fullmatch(line)
        require(m is not None, f"invalid manifest row: {line}")
        ident, tags, expected, oracle, count, _function = m.groups()
        require(ident and tags and int(count) > 0, "invalid manifest metadata")
        rows.append((ident, tags, expected, oracle, int(count)))
    require(len(rows) == EXPECTED_FAMILIES, "manifest family count")
    require(len({row[0] for row in rows}) == len(rows), "duplicate manifest IDs")
    require(sum(row[4] for row in rows) == EXPECTED_FIXTURES,
            "manifest fixture count")
    return rows


def shuffled_order(n, seed):
    order = list(range(n))
    state = seed
    if seed:
        for i in range(n - 1, 0, -1):
            state ^= (state << 13) & MASK32
            state ^= state >> 17
            state ^= (state << 5) & MASK32
            state &= MASK32
            j = state % (i + 1)
            order[i], order[j] = order[j], order[i]
    return order


def fingerprint(order):
    h = 0xCBF29CE484222325
    for index in order:
        value = index + 1
        for shift in (0, 8, 16, 24):
            h ^= (value >> shift) & 255
            h = (h * 0x100000001B3) & MASK64
    return f"{h:016x}"


def exact_keys(obj, keys, where):
    require(type(obj) is dict and set(obj) == set(keys),
            f"{where}: missing, duplicate or unexpected keys")


def strict_int(value, expected, where):
    require(type(value) is int and value == expected, f"{where}: wrong integer")


def validate_receipt(report, rows):
    exact_keys(report, (
        "schema_version", "corpus_id", "fixture_kind", "result",
        "family_count", "fixture_count", "shuffle_protocol", "round_count",
        "total_fixture_executions", "round_results", "family_results",
    ), "report")
    strict_int(report["schema_version"], 2, "schema")
    require(report["corpus_id"] == "native-source-pdf" and
            report["fixture_kind"] == "generated" and
            report["result"] == "passed", "report identity/result")
    strict_int(report["family_count"], EXPECTED_FAMILIES, "family_count")
    strict_int(report["fixture_count"], EXPECTED_FIXTURES, "fixture_count")
    strict_int(report["round_count"], len(SEEDS), "round_count")
    strict_int(report["total_fixture_executions"],
               EXPECTED_FIXTURES * len(SEEDS), "total_fixture_executions")
    require(report["shuffle_protocol"] == "xorshift32-fisher-yates-v1",
            "shuffle protocol drift")

    family_results = report["family_results"]
    require(type(family_results) is list and
            len(family_results) == len(rows), "family_results cardinality")
    for i, (result, source) in enumerate(zip(family_results, rows)):
        exact_keys(result, (
            "id", "feature_tags", "expected_outcome", "oracle",
            "declared_fixtures", "executed_fixtures", "result",
        ), f"family[{i}]")
        ident, tags, outcome, oracle, count = source
        require(result["id"] == ident and result["feature_tags"] == tags and
                result["expected_outcome"] == outcome and
                result["oracle"] == oracle and result["result"] == "passed",
                f"family[{i}] metadata mismatch")
        strict_int(result["declared_fixtures"], count, f"family[{i}] declared")
        strict_int(result["executed_fixtures"], count, f"family[{i}] executed")

    rounds = report["round_results"]
    require(type(rounds) is list and len(rounds) == len(SEEDS),
            "round_results cardinality")
    ids = [row[0] for row in rows]
    for k, (rec, seed, fixed_hash) in enumerate(zip(rounds, SEEDS, FINGERPRINTS)):
        exact_keys(rec, (
            "order", "seed", "fingerprint_fnv64",
            "observed_fixture_count", "family_order", "observed_fixtures",
        ), f"round[{k}]")
        expected_indices = shuffled_order(len(rows), seed)
        expected_ids = [ids[i] for i in expected_indices]
        expected_counts = [rows[i][4] for i in expected_indices]
        require(rec["order"] == ("canonical" if k == 0 else "seeded-shuffle"),
                f"round[{k}] label")
        require(rec["seed"] == f"{seed:08x}", f"round[{k}] seed")
        require(fingerprint(expected_indices) == fixed_hash and
                rec["fingerprint_fnv64"] == fixed_hash, f"round[{k}] hash")
        require(type(rec["family_order"]) is list and
                rec["family_order"] == expected_ids, f"round[{k}] permutation")
        require(type(rec["observed_fixtures"]) is list and
                rec["observed_fixtures"] == expected_counts and
                all(type(x) is int for x in rec["observed_fixtures"]),
                f"round[{k}] per-family executions")
        strict_int(rec["observed_fixture_count"],
                   EXPECTED_FIXTURES, f"round[{k}] fixtures")


def no_duplicate_keys(pairs):
    result = {}
    for name, value in pairs:
        require(name not in result, f"duplicate JSON key: {name}")
        result[name] = value
    return result


def example_receipt(rows):
    families = []
    for ident, tags, expected, oracle, count in rows:
        families.append(dict(
            id=ident, feature_tags=tags, expected_outcome=expected,
            oracle=oracle, declared_fixtures=count, executed_fixtures=count,
            result="passed",
        ))
    rounds = []
    for k, seed in enumerate(SEEDS):
        order = shuffled_order(len(rows), seed)
        rounds.append(dict(
            order="canonical" if k == 0 else "seeded-shuffle",
            seed=f"{seed:08x}", fingerprint_fnv64=fingerprint(order),
            observed_fixture_count=EXPECTED_FIXTURES,
            family_order=[rows[i][0] for i in order],
            observed_fixtures=[rows[i][4] for i in order],
        ))
    return dict(
        schema_version=2, corpus_id="native-source-pdf",
        fixture_kind="generated", result="passed",
        family_count=EXPECTED_FAMILIES, fixture_count=EXPECTED_FIXTURES,
        shuffle_protocol="xorshift32-fisher-yates-v1",
        round_count=len(SEEDS),
        total_fixture_executions=EXPECTED_FIXTURES * len(SEEDS),
        round_results=rounds, family_results=families,
    )


def negative_self_tests(rows):
    baseline = example_receipt(rows)
    validate_receipt(baseline, rows)
    corruptions = (
        lambda r: r["round_results"][1].update(seed="00000001"),
        lambda r: r["round_results"][2]["family_order"].reverse(),
        lambda r: r["round_results"][1]["observed_fixtures"].pop(),
        lambda r: r["round_results"].pop(),
        lambda r: r["family_results"][0].update(executed_fixtures=0),
        lambda r: r.update(total_fixture_executions=58),
        lambda r: r.update(schema_version=1),
    )
    for i, mutate in enumerate(corruptions):
        tampered = copy.deepcopy(baseline)
        mutate(tampered)
        try:
            validate_receipt(tampered, rows)
        except InvalidReceipt:
            continue
        raise InvalidReceipt(f"negative self-test {i} was accepted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    rows = load_manifest(args.manifest)
    negative_self_tests(rows)
    report = json.loads(args.report.read_text(encoding="utf-8"),
                        object_pairs_hook=no_duplicate_keys)
    validate_receipt(report, rows)
    print(f"[source-corpus-receipt] verified {len(rows)} families / "
          f"{EXPECTED_FIXTURES} unique fixtures / {len(SEEDS)} rounds / "
          f"{EXPECTED_FIXTURES * len(SEEDS)} executions; exact seeds, "
          "fingerprints, observations and manifest parity")


if __name__ == "__main__":
    try:
        main()
    except (InvalidReceipt, OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"[source-corpus-receipt] failed: {error}") from error
