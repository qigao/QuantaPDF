#!/usr/bin/env python3
"""Test-only original jsPDF PDFium bitmap mapping hypotheses (Issue #269).

External caller explicitly supplies upstream immutable source and the built
Graph test binary. No download, source re-export, file copies, Native raster,
SDK cutover, official corpus promotion, or bitmap artifact upload.

Hypothesis A: PDFium FPDF_RenderPageBitmap() fits fractional page extents to
              the entire integer viewport.
Hypothesis B: FPDF_RenderPageBitmapWithMatrix() uses a uniform physical
              source-point DPI scale and clips to that SAME integer viewport.
These are observations, not interchangeable aliases or a Native pixel match.
"""
import argparse
import json
import math
from pathlib import Path
import re
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

EXPECTED_DIMENSIONS = {
    72: (596, 842),
    96: (794, 1123),
    144: (1191, 1684),
}
EXPECTED_MODES = ["fitted-integer-viewport", "physical-dpi-matrix"]
FNV = re.compile(r"[0-9a-f]{16}\Z")


def _bounded_integer(value: Any, minimum: int, maximum: int,
                     context: str) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise QualificationFailure(f"bad/missing {context}")
    return value


def _bounded_float(value: Any, minimum: float, maximum: float,
                   context: str) -> float:
    if type(value) not in (int, float) or not math.isfinite(float(value)):
        raise QualificationFailure(f"nonfinite/missing {context}")
    number = float(value)
    if not minimum <= number <= maximum:
        raise QualificationFailure(f"out-of-range {context}")
    return number


def validate_probe(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise QualificationFailure("probe is not an object")
    contract = {
        "schema_version": 1,
        "test_id": "issue-269-original-pdfium-viewport-hypotheses",
        "fixture_kind": "external-original-unvendored",
        "reference_kind": "direct-public-pdfium-c-api-two-independent-modes",
        "pixel_budget": 1_000_000,
        "bitmap_render_count": 8,
        "models_are_pdfium_only": True,
        "native_raster_invoked": False,
        "native_pixel_positive": False,
        "official_corpus_admitted": False,
        "production_decoder_changed": False,
        "result": "two-hypothesis-diagnostic-only",
    }
    for key, expected in contract.items():
        if type(value.get(key)) is not type(expected) or value[key] != expected:
            raise QualificationFailure(f"probe contract violation: {key}")
    bounds = value.get("pdfium_original_page_points")
    if not isinstance(bounds, list) or len(bounds) != 2:
        raise QualificationFailure("original PDFium dimensions missing")
    for measured, expected in zip(bounds, (595.28, 841.89)):
        _bounded_float(measured, expected - 0.01, expected + 0.01,
                       "original page bounds")
    cases = value.get("dpi_cases")
    if not isinstance(cases, list) or len(cases) != len(EXPECTED_DIMENSIONS):
        raise QualificationFailure("wrong number of DPI hypotheses")
    for entry, (dpi, (width, height)) in zip(cases, EXPECTED_DIMENSIONS.items()):
        if not isinstance(entry, dict) or type(entry.get("dpi")) is not int or (
            entry["dpi"] != dpi or entry.get("expected_grid") != [width, height]
        ):
            raise QualificationFailure(f"grid mismatch for DPI {dpi}")
        if dpi == 144:
            if entry.get("status") != "unsupported-capacity" or (
                entry.get("pdfium_bitmap_allocated") is not False
            ) or entry.get("candidate_output") != [0, 0]:
                raise QualificationFailure("oversized DPI bitmap was not rejected")
            continue
        if entry.get("status") != "diagnostic-only" or (
            entry.get("pdfium_bitmap_allocated") is not True
        ) or entry.get("bitmap_modes") != EXPECTED_MODES or (
            entry.get("reference_repeats_identical") is not True
        ) or entry.get("upstream_clip_red_interior_goldens") is not True:
            raise QualificationFailure(f"invalid real-reference bitmap mode at DPI {dpi}")
        n_pixels = width * height
        for name in ("fitted_nonwhite_pixels", "physical_nonwhite_pixels"):
            _bounded_integer(entry.get(name), 100, n_pixels // 2 - 1, name)
        difference = _bounded_integer(entry.get("differing_rgb_pixels"), 0,
                                      n_pixels, "RGB differing pixel count")
        _bounded_integer(entry.get("total_rgb_channel_absolute_difference"),
                         0, n_pixels * 3 * 255, "RGB channel L1")
        largest = _bounded_integer(entry.get("largest_rgb_channel_difference"),
                                   0, 255, "largest RGB channel delta")
        if (difference == 0) != (largest == 0):
            raise QualificationFailure("RGB max-delta and differing-pixel mismatch")
        for name in ("fitted_rgb_fnv64", "physical_rgb_fnv64"):
            if not isinstance(entry.get(name), str) or not FNV.fullmatch(
                entry[name]
            ):
                raise QualificationFailure(f"missing/bad original PDFium RGB digest {name}")
        expected_scale = dpi / 72
        fitted = entry.get("fitted_scale")
        if not isinstance(fitted, list) or len(fitted) != 2:
            raise QualificationFailure("invalid fitted bitmap scales")
        for scale, page_points, extent in zip(fitted, bounds, (width, height)):
            value_scale = _bounded_float(scale, 0.1, 10, "fitted scale")
            if abs(value_scale - extent / page_points) > 1e-10 or (
                value_scale <= expected_scale
            ):
                raise QualificationFailure("fitted integer grid vs source extent incorrect")
        physical = _bounded_float(entry.get("physical_scale"),
                                  0.1, 10, "physical DPI scale")
        if abs(physical - expected_scale) > 1e-10:
            raise QualificationFailure("nonphysical scalar DPI map")
        rightmost_fit = _bounded_float(
            entry.get("rightmost_center_source_x_fitted"),
            0, 10000, "fitted rightmost pixel center")
        rightmost_physical = _bounded_float(
            entry.get("rightmost_center_source_x_physical"),
            0, 10000, "physical rightmost pixel center")
        if not (
            abs(rightmost_fit - (width - 0.5) / fitted[0]) < 1e-9
            and abs(rightmost_physical - (width - 0.5) / expected_scale) < 1e-9
            and rightmost_physical > rightmost_fit
        ):
            raise QualificationFailure("rightmost pixel center affine maps conflated")
    return value


def run(source: Path, executable: Path, report: Path) -> None:
    source = source.resolve()
    executable = executable.resolve()
    report = report.resolve()
    if len({str(source), str(executable), str(report)}) != 3:
        raise QualificationFailure("source/executable/report paths overlap")
    staged = report.with_name(report.name + ".tmp")
    report.unlink(missing_ok=True)
    staged.unlink(missing_ok=True)
    if not executable.is_file():
        raise QualificationFailure("private PDFium observation binary is missing")
    original = pin_original_source(source)
    try:
        result = subprocess.run(
            [str(executable), "--quarantined-jspdf-pixel-map", str(source)],
            capture_output=True, text=True, check=False, timeout=100,
        )
        if result.returncode != 0:
            raise QualificationFailure(
                "original PDFium two-hypothesis rendering failed: "
                + result.stderr[-1500:]
            )
        try:
            observations = validate_probe(json.loads(result.stdout))
        except (ValueError, TypeError) as exc:
            raise QualificationFailure("PDFium mapping probe emitted invalid JSON") from exc
        if pin_original_source(source) != original:
            raise QualificationFailure("original jsPDF PDF changed during bitmap observation")
        final = {
            "schema_version": 1,
            "qualification": "quarantined-original-pdfium-pixel-mapping",
            "source": {
                "original_url": UPSTREAM_URL,
                "original_revision": UPSTREAM_REVISION,
                "original_git_blob": UPSTREAM_GIT_BLOB,
                "sha256": EXPECTED_SOURCE_SHA256,
                "size_bytes": EXPECTED_SOURCE_BYTES,
                "source_immutable": True,
                "redistribution_review": "pending",
            },
            "probe": observations,
            "native_pixel_positive": False,
            "original_native_raster_invoked": False,
            "bitmap_artifacts_published": False,
            "official_corpus_admitted": False,
            "production_backend_changed": False,
        }
        write_json_lf(staged, final)
        staged.replace(report)
    except Exception:
        staged.unlink(missing_ok=True)
        report.unlink(missing_ok=True)
        raise


def self_test() -> None:
    bad = {
        "schema_version": 1,
        "test_id": "issue-269-original-pdfium-viewport-hypotheses",
        "fixture_kind": "external-original-unvendored",
        "native_pixel_positive": True,
    }
    try:
        validate_probe(bad)
    except QualificationFailure:
        pass
    else:
        raise AssertionError("forged Native pixel positive was accepted")
    # A forged physical-vs-fit report must never be accepted on two
    # equal/artificial axes; strict viewport transformation provenance.
    with tempfile.TemporaryDirectory(prefix="qp269-pixel-map-") as td:
        directory = Path(td)
        wrong = directory / "wrong-original.pdf"
        exe = directory / "fake-exe"
        report = directory / "stale-result.json"
        wrong.write_bytes(b"%PDF-1.3\nthis is NOT a pinned original")
        report.write_text('{"result":"passed"}', encoding="utf-8")
        try:
            run(wrong, exe, report)
        except QualificationFailure:
            pass
        else:
            raise AssertionError("missing executable accepted")
        assert not report.exists()
        exe.write_bytes(b"not-an-executable")
        report.write_text('{"result":"passed"}', encoding="utf-8")
        try:
            run(wrong, exe, report)
        except QualificationFailure:
            pass
        else:
            raise AssertionError("untrusted source bytes accepted")
        assert not report.exists()
        output = directory / "canonical.json"
        write_json_lf(output, {"a": [1, 2], "z": False})
        payload = output.read_bytes()
        assert payload.endswith(b"\n") and b"\r" not in payload
    print("[issue-269] original PDFium viewport-model guard self-test PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.self_test:
        if any((args.source, args.executable, args.report)):
            parser.error("--self-test accepts no source/executable/report")
        self_test()
        return 0
    if not all((args.source, args.executable, args.report)):
        parser.error("--source, --executable and --report are required")
    try:
        run(args.source, args.executable, args.report)
    except (QualificationFailure, OSError, subprocess.TimeoutExpired) as exc:
        print(f"[issue-269] quarantined PDFium mapping observation denied: {exc}",
              file=sys.stderr)
        return 1
    print("[issue-269] original PDFium fit-vs-physical DPI mapping report:", args.report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
