#!/usr/bin/env python3
"""SHA-pinned, opt-in source->detached IR->Native rejection observation (#269).

The user/CI supplies the original jsPDF source PDF and a separately built
private native-source test executable. This script does not download, vendor,
re-export, rasterize via PDFium, or admit an external source corpus document.

A successful *diagnostic* may report blocked-before-native, and this MUST
NOT be interpreted as qualified native raster zero-output rejection.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any

from quarantined_jspdf_geometry import (
    EXPECTED_SOURCE_BYTES,
    EXPECTED_SOURCE_SHA256,
    UPSTREAM_GIT_BLOB,
    UPSTREAM_REVISION,
    UPSTREAM_URL,
    QualificationFailure,
    pin_original_source,
    write_json_lf,
)

EXPECTED_KEYS: dict[str, Any] = {
    "schema_version": 1,
    "test_id": "issue-269-quarantined-original-native-negative",
    "fixture_kind": "external-original-unvendored",
    "source_was_reexported": False,
    "pdfium_bitmap_created": False,
    "external_native_pixel_positive_increment": 0,
    "official_corpus_admitted": False,
    "production_raster_changed": False,
}


def validate_native_observation(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise QualificationFailure("native observation is not a JSON object")
    for name, expected in EXPECTED_KEYS.items():
        actual = value.get(name)
        if type(actual) is not type(expected) or actual != expected:
            raise QualificationFailure(f"native observation has invalid {name}")
    lowered = value.get("source_to_detached_ir")
    attempted = value.get("native_raster_attempted")
    native_status = value.get("native_status")
    zero_output = value.get("native_output_zero")
    repeats = value.get("native_render_repeat_count")
    status_code = value.get("lowering_status_code")
    pages = value.get("detached_page_count")
    operations = value.get("detached_path_operation_count")
    clips = value.get("detached_clip_node_count")
    states = value.get("detached_graphics_state_count")
    page_points = value.get("detached_page_points")
    result = value.get("result")
    if (type(status_code) is not int or type(pages) is not int or
        type(operations) is not int or type(clips) is not int or
        type(states) is not int or
        not isinstance(page_points, list) or len(page_points) != 2 or
        any(type(x) not in (int, float) for x in page_points)):
        raise QualificationFailure("native observation lacks bounded source/IR metadata")
    if lowered == "lowered":
        if (status_code != 0 or pages != 1 or
            operations < 1 or operations > 4 or
            clips < 1 or clips > 8 or states < 0 or states > 4 or
            not all(abs(float(x)-expected) < 0.01
                    for x,expected in zip(page_points,(595.28,841.89))) or
            attempted is not True or native_status != "unsupported" or
            zero_output is not True or repeats != 2 or
            result != "native-fail-closed"):
            raise QualificationFailure("qualified native denial lacks original detached IR evidence")
    elif lowered in ("unsupported", "format-rejected"):
        if (status_code == 0 or pages != 0 or
            operations != 0 or clips != 0 or states != 0 or
            page_points != [0,0] or attempted is not False or
            native_status != "not-invoked" or zero_output is not None or
            repeats != 0 or result != "blocked-before-native"):
            raise QualificationFailure("earlier source lowering refusal wrongly claims native raster acceptance")
    else:
        raise QualificationFailure("unrecognized native source stage")
    return value


def run(source: Path, executable: Path, report: Path,
        require_native_denial: bool = False) -> dict[str, Any]:
    source = source.resolve()
    executable = executable.resolve()
    report = report.resolve()
    if len({str(source),str(executable),str(report)}) != 3:
        raise QualificationFailure("source, executable and report must be distinct")
    report.unlink(missing_ok=True)
    staged = report.with_name(report.name + ".tmp")
    staged.unlink(missing_ok=True)
    if not executable.is_file():
        raise QualificationFailure("built native-source test executable missing")
    original = pin_original_source(source)
    try:
        completed = subprocess.run(
            [str(executable), "--quarantined-jspdf-native-negative", str(source)],
            capture_output=True,
            text=True,
            check=False,
            timeout=90,
        )
        if completed.returncode != 0:
            raise QualificationFailure(
                "native source observation failed: " + completed.stderr[-1000:]
            )
        try:
            native = validate_native_observation(json.loads(completed.stdout))
        except (ValueError, TypeError) as exc:
            raise QualificationFailure("native source observation is not valid JSON") from exc
        if pin_original_source(source) != original:
            raise QualificationFailure("immutable source changed during native observation")
        accepted = native["result"] == "native-fail-closed"
        if require_native_denial and not accepted:
            raise QualificationFailure(
                "source lowering stopped before Native Raster; "
                "no qualified native zero-output rejection"
            )
        record = {
            "schema_version": 1,
            "qualification": "quarantined-native-negative-diagnostic",
            "source": {
                "original_url": UPSTREAM_URL,
                "original_revision": UPSTREAM_REVISION,
                "original_git_blob": UPSTREAM_GIT_BLOB,
                "sha256": EXPECTED_SOURCE_SHA256,
                "size_bytes": EXPECTED_SOURCE_BYTES,
                "source_immutable": True,
                "redistribution_review": "pending",
            },
            "native_observation": native,
            "qualified_native_zero_output_denial": accepted,
            "native_pixel_positive": False,
            "official_corpus_admitted": False,
            "production_backend_changed": False,
        }
        write_json_lf(staged, record)
        staged.replace(report)
        return record
    except Exception:
        staged.unlink(missing_ok=True)
        report.unlink(missing_ok=True)
        raise


def self_test() -> None:
    baseline = {
        **EXPECTED_KEYS,
        "source_to_detached_ir": "unsupported",
        "lowering_status_code": 4,
        "detached_page_count": 0,
        "detached_path_operation_count": 0,
        "detached_clip_node_count": 0,
        "detached_graphics_state_count": 0,
        "detached_page_points": [0, 0],
        "native_raster_attempted": False,
        "native_status": "not-invoked",
        "native_output_zero": None,
        "native_render_repeat_count": 0,
        "result": "blocked-before-native",
    }
    validate_native_observation(baseline)
    # Failure to lower must never be reinterpreted as a native zero reject.
    for change in (
        {"native_raster_attempted": True},
        {"native_output_zero": True},
        {"native_status": "unsupported"},
        {"result": "native-fail-closed"},
        {"external_native_pixel_positive_increment": 1},
        {"pdfium_bitmap_created": True},
    ):
        try:
            validate_native_observation({**baseline, **change})
        except QualificationFailure:
            pass
        else:
            raise AssertionError(f"forged native-positive observation accepted: {change}")
    valid_native = {
        **baseline,
        "source_to_detached_ir": "lowered",
        "lowering_status_code": 0,
        "detached_page_count": 1,
        "detached_path_operation_count": 2,
        "detached_clip_node_count": 1,
        "detached_graphics_state_count": 1,
        "detached_page_points": [595.280029297, 841.890014648],
        "native_raster_attempted": True,
        "native_status": "unsupported",
        "native_output_zero": True,
        "native_render_repeat_count": 2,
        "result": "native-fail-closed",
    }
    validate_native_observation(valid_native)
    for change in (
        {"native_output_zero": False},
        {"detached_clip_node_count": 0},
        {"detached_page_count": 0},
        {"native_render_repeat_count": 1},
        {"native_status": "ok"},
    ):
        try:
            validate_native_observation({**valid_native, **change})
        except QualificationFailure:
            pass
        else:
            raise AssertionError(f"forged native-negative observation accepted: {change}")
    with tempfile.TemporaryDirectory(prefix="qp269-native-denial-") as td:
        root = Path(td)
        source = root / "wrong-original.pdf"
        executable = root / "not-an-executable"
        report = root / "report.json"
        source.write_bytes(b"%PDF-1.3\nnot-original")
        report.write_bytes(b'{"result":"passed"}')
        try:
            run(source, executable, report)
        except QualificationFailure:
            pass
        else:
            raise AssertionError("missing executable accepted")
        assert not report.exists()
        executable.write_bytes(b"not actually executable")
        report.write_bytes(b'{"result":"passed"}')
        try:
            run(source, executable, report)
        except QualificationFailure:
            pass
        else:
            raise AssertionError("untrusted source accepted")
        assert not report.exists()
    print("[issue-269] original-source native negative guard self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--require-native-denial", action="store_true",
                        help="fail closed if source lowering blocks before Native")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.self_test:
        if any((args.source, args.executable, args.report)) or args.require_native_denial:
            parser.error("--self-test takes no other arguments")
        self_test()
        return 0
    if not all((args.source, args.executable, args.report)):
        parser.error("--source, --executable and --report are required together")
    try:
        observation = run(args.source, args.executable, args.report,
                          require_native_denial=args.require_native_denial)
    except (QualificationFailure, OSError, subprocess.TimeoutExpired) as exc:
        print(f"[issue-269] source native rejection observation refused: {exc}",
              file=sys.stderr)
        return 1
    print("[issue-269] source-to-IR/native diagnostic:",
          observation["native_observation"]["result"],
          "- report:", args.report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
