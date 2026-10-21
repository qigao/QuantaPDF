#!/usr/bin/env python3
"""Optional, non-vendoring geometry-only test for jsPDF's pinned w3s_clip.pdf.

No URL requests, downloads, PDF file copies, provider fallback, PDF raster,
formal external-corpus admissions, or production SDK/raster changes occur.
The operator supplies an unchanged local copy of the independently published
PDF and the already-built test_graph_page_geometry executable.

Example:
  python tools/qualification/quarantined_jspdf_geometry.py \
    --source /tmp/upstream-w3s_clip.pdf \
    --executable build/linux-gcc-release/tests/quantapdf_test_graph_page_geometry \
    --report /tmp/269-original-geometry-quarantined.json

The source document is hash-validated both before and after invoking the
geometry-only probe. A report is published atomically only on full success.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any

EXPECTED_SOURCE_SHA256 = (
    "179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e"
)
EXPECTED_SOURCE_BYTES = 3344
UPSTREAM_REVISION = "20d32998267745aa5336e299baa65d4f45916fb5"
UPSTREAM_GIT_BLOB = "997ed4fbb327289740d7db1dc45344f1e1233fb9"
UPSTREAM_URL = (
    "https://github.com/parallax/jsPDF/blob/"
    + UPSTREAM_REVISION + "/test/reference/w3s_clip.pdf"
)
SOURCE_BOX = b"/MediaBox [0 0 595.28 841.89]"
EXPECTED_GRIDS = {
    72: ((596, 842), "admitted", (596, 842)),
    96: ((794, 1123), "admitted", (794, 1123)),
    144: ((1191, 1684), "unsupported-capacity", (0, 0)),
}


class QualificationFailure(Exception):
    """The input, geometry evidence, or source immutability is unqualified."""


def pin_original_source(path: Path) -> bytes:
    if not path.is_file():
        raise QualificationFailure("source PDF is not an existing regular file")
    # Reject overlarge documents without unbounded memory buffering.
    with path.open("rb") as fh:
        data = fh.read(EXPECTED_SOURCE_BYTES + 1)
    if len(data) != EXPECTED_SOURCE_BYTES:
        raise QualificationFailure("source PDF byte length does not match pinned original")
    if hashlib.sha256(data).hexdigest() != EXPECTED_SOURCE_SHA256:
        raise QualificationFailure("source PDF SHA-256 does not match pinned original")
    if not data.startswith(b"%PDF-1.3") or SOURCE_BOX not in data:
        raise QualificationFailure("source PDF's pinned syntax evidence is missing")
    return data


def admit_geometry(probe: Any) -> dict[str, Any]:
    if not isinstance(probe, dict):
        raise QualificationFailure("geometry probe did not emit an object")
    required = {
        "schema_version": 1,
        "test_id": "issue-269-quarantined-original-geometry",
        "fixture_kind": "external-original-unvendored",
        "source_size_bytes": EXPECTED_SOURCE_BYTES,
        "source_user_unit": 1,
        "source_rotation": 0,
        "pixel_budget": 1000000,
        "pdfium_bitmap_created": False,
        "native_raster_invoked": False,
        "external_native_positive_increment": 0,
        "official_corpus_admitted": False,
        "result": "geometry-only-diagnostic",
    }
    for k, v in required.items():
        if type(probe.get(k)) is not type(v) or probe[k] != v:
            raise QualificationFailure(f"invalid/missing geometry field: {k}")
    cases = probe.get("grid_cases")
    if not isinstance(cases, list) or len(cases) != 3:
        raise QualificationFailure("expected exactly three deterministic DPI cases")
    for case, (dpi, values) in zip(cases, EXPECTED_GRIDS.items()):
        expect_size, expect_status, expect_output = values
        if (not isinstance(case, dict) or case.get("dpi") != dpi or
            case.get("expected_grid") != list(expect_size) or
            case.get("pdfium_geometry_grid") != list(expect_size) or
            case.get("rational_candidate_status") != expect_status or
            case.get("rational_candidate_output") != list(expect_output)):
            raise QualificationFailure(f"invalid original geometry observation at DPI {dpi}")
        graph = case.get("graph_ceil_diagnostic")
        if (not isinstance(graph, list) or len(graph) != 2 or
            any(type(x) is not int for x in graph) or
            any(abs(a-b) > 1 for a, b in zip(graph, expect_size))):
            raise QualificationFailure(f"Graph estimate missing/out of range at DPI {dpi}")
    pdfium_points = probe.get("pdfium_original_page_points")
    graph_points = probe.get("graph_display_points")
    if not (isinstance(pdfium_points, list) and len(pdfium_points) == 2 and
            isinstance(graph_points, list) and len(graph_points) == 2):
        raise QualificationFailure("missing PDFium/Graph page points")
    for measured in (pdfium_points, graph_points):
        for actual, expected in zip(measured, (595.28, 841.89)):
            if (type(actual) not in (int, float) or
                not abs(float(actual) - expected) < 0.01):
                raise QualificationFailure("unexpected original page geometry")
    return probe


def run(source: Path, executable: Path, report: Path) -> None:
    source = source.resolve()
    report = report.resolve()
    executable = executable.resolve()
    if source == report or report == executable or source == executable:
        raise QualificationFailure("source, executable and report must be distinct")
    # Never let stale successful artifacts survive ANY failed preflight,
    # including a missing executable or invalid original input.
    report.unlink(missing_ok=True)
    staged = report.with_name(report.name + ".tmp")
    staged.unlink(missing_ok=True)
    if not executable.is_file():
        raise QualificationFailure("test executable does not exist")
    original = pin_original_source(source)
    try:
        process = subprocess.run(
            [str(executable), "--quarantined-jspdf-geometry", str(source)],
            check=False,
            capture_output=True,
            text=True,
            timeout=90,
        )
        if process.returncode != 0:
            raise QualificationFailure(
                "native geometry probe failed: " + process.stderr[-1200:]
            )
        try:
            probe = admit_geometry(json.loads(process.stdout))
        except (ValueError, TypeError) as exc:
            raise QualificationFailure("geometry probe did not emit valid JSON") from exc
        if pin_original_source(source) != original:
            raise QualificationFailure("original source PDF mutated during probe")
        result = {
            "schema_version": 1,
            "qualification": "quarantined-geometry-only",
            "source": {
                "original_url": UPSTREAM_URL,
                "original_revision": UPSTREAM_REVISION,
                "git_blob": UPSTREAM_GIT_BLOB,
                "sha256": EXPECTED_SOURCE_SHA256,
                "size_bytes": EXPECTED_SOURCE_BYTES,
                "source_immutable": True,
                "redistribution_review": "pending",
            },
            "probe": probe,
            "native_pixel_positive": False,
            "official_corpus_admitted": False,
            "production_backend_changed": False,
        }
        staged.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                          encoding="utf-8")
        staged.replace(report)
    except Exception:
        staged.unlink(missing_ok=True)
        report.unlink(missing_ok=True)
        raise


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="qp269-source-guard-") as td:
        root = Path(td)
        source = root / "not-original.pdf"
        for payload in (b"", b"%PDF-1.3\n", SOURCE_BOX, b"A" * EXPECTED_SOURCE_BYTES):
            source.write_bytes(payload)
            try:
                pin_original_source(source)
            except QualificationFailure:
                pass
            else:
                raise AssertionError("unverified original bytes were accepted")
        # Good-looking but forged report metadata is not enough.
        try:
            admit_geometry({
                "schema_version": 1, "test_id": "issue-269-quarantined-original-geometry",
                "fixture_kind": "external-original-unvendored",
                "native_raster_invoked": True,
            })
        except QualificationFailure:
            pass
        else:
            raise AssertionError("unqualified source/renderer observation accepted")
        # Source failure must remove any stale report before the subprocess.
        bad_executable = root / "never-run"
        fake_report = root / "stale.json"
        fake_report.write_text('{"result":"passed"}', encoding="utf-8")
        try:
            run(source, bad_executable, fake_report)
        except QualificationFailure:
            pass
        else:
            raise AssertionError("invalid executable was accepted")
        assert not fake_report.exists()
        # Invalid upstream original bytes, even with a plausible executable,
        # must refuse without leaving a false successful report.
        fake_exe = root / "dummy-executable"
        fake_exe.write_text("#!/usr/bin/env python3\n", encoding="utf-8")
        fake_report.write_text('{"result":"passed"}', encoding="utf-8")
        try:
            run(source, fake_exe, fake_report)
        except QualificationFailure:
            pass
        else:
            raise AssertionError("incorrect source SHA was accepted")
        assert not fake_report.exists()
        # No external file nor output was moved/vendored into the repository.
    print("[issue-269] quarantined upstream SHA/geometry guard: negative cases passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.self_test:
        if any((args.source, args.executable, args.report)):
            parser.error("--self-test takes no other arguments")
        self_test()
        return 0
    if not all((args.source, args.executable, args.report)):
        parser.error("--source, --executable and --report are all required")
    try:
        run(args.source, args.executable, args.report)
    except (QualificationFailure, OSError, subprocess.TimeoutExpired) as exc:
        print(f"[issue-269] quarantine admission refused: {exc}", file=sys.stderr)
        return 1
    print(f"[issue-269] SHA-pinned geometry-only report published: {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
