#!/usr/bin/env python3
"""Attest M2 Native rendering with exact source samples and bounded full-RGB comparison.

No download, vendoring, bitmap artifact, corpus promotion, or SDK switch.
The supplied executable must be the separately built Native source qualification test.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys
import tempfile

SOURCE_SHA256 = "179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e"
SOURCE_SIZE = 3344
SOURCE_REVISION = "20d32998267745aa5336e299baa65d4f45916fb5"
SOURCE_URL = ("https://github.com/parallax/jsPDF/blob/" + SOURCE_REVISION +
              "/test/reference/w3s_clip.pdf")
EXPECTED_LINES = [
    "M2 unchanged jsPDF dpi=72 expected=596x842 plan=admitted Native=positive-repeatable-full-rgb-source-samples; native_pixel_positive=true",
    "M2 unchanged jsPDF dpi=96 expected=794x1123 plan=admitted Native=positive-repeatable-full-rgb-source-samples; native_pixel_positive=true",
    "M2 unchanged jsPDF dpi=144 expected=1191x1684 plan=capacity-rejected Native=not-invoked; native_pixel_positive=false",
]


class QualificationFailure(Exception):
    pass


def pinned_source(path):
    with path.open("rb") as stream:
        value = stream.read(SOURCE_SIZE + 1)
    if len(value) != SOURCE_SIZE or hashlib.sha256(value).hexdigest() != SOURCE_SHA256:
        raise QualificationFailure("source bytes do not match the pinned original")
    return value


RGB_LINE = re.compile(
    r"M2 RGB dpi=(72|96) different=([0-9]+) boundary=([0-9]+) L1=([0-9]+) "
    r"budget=([0-9]+) max=([0-9]+) red=([0-9]+) stroke=([0-9]+) "
    r"outside=([0-9]+) clip_white=([0-9]+) samples=exact")


def observation(stdout):
    lines = stdout.splitlines()
    if len(lines) != 5 or [lines[1], lines[3], lines[4]] != EXPECTED_LINES:
        raise QualificationFailure("incomplete or changed M2 device/Native RGB observation")
    cases = []
    for index, (dpi, width, height) in enumerate(((72, 596, 842), (96, 794, 1123))):
        match = RGB_LINE.fullmatch(lines[2 * index])
        if not match:
            raise QualificationFailure("missing exact-source full-RGB metrics")
        measured_dpi, different, boundary, l1, budget, maximum, red, stroke, outside, clip_white = map(int, match.groups())
        pixels = width * height
        if not (measured_dpi == dpi and 0 <= different <= boundary <= pixels and
                0 <= l1 <= budget <= boundary * 3 * 255 and 0 <= maximum <= 255 and
                maximum <= l1 <= 3 * different * maximum and
                (different == 0) == (maximum == 0) and
                all(0 < region <= pixels for region in (red, stroke, outside, clip_white))):
            raise QualificationFailure("invalid source/RGB metrics or exceeded boundary budget")
        cases.append({
            "dpi": dpi, "grid": [width, height], "plan": "admitted",
            "detached_paths": 2, "detached_clips": 1, "detached_graphics_states": 1,
            "source_and_snapshot_disposed_before_raster": True,
            "native_status": "ok", "native_output_zero": False,
            "native_render_count": 2, "repeat_identical": True,
            "source_sample_oracle": "exact-rational-4x4-stroke-then-clipped-fill",
            "full_rgb_parity_verified": True,
            "rgb_comparison": "exact-off-source-boundaries-bounded-on-boundaries",
            "pdfium_pixel_identical": different == 0,
            "rgb_metrics": {"different_pixels": different, "boundary_pixels": boundary,
                            "l1": l1, "l1_budget": budget, "max_channel_delta": maximum},
            "regions": {"red_fill": red, "black_stroke": stroke,
                        "transparent_outside": outside, "white_clip_interior": clip_white},
        })
    return cases + [{"dpi": 144, "expected_grid": [1191, 1684],
                     "plan": "capacity-rejected", "plan_output_zero": True,
                     "ir_output_zero": True, "native_render_count": 0,
                     "native_invoked": False}]


def reject_aliases(paths):
    for i, first in enumerate(paths):
        for second in paths[i + 1:]:
            if first.resolve() == second.resolve() or (
                first.exists() and second.exists() and first.samefile(second)
            ):
                raise QualificationFailure("source, executable, report and staging paths must be distinct")


def run(source, executable, report):
    source, executable = source.resolve(), executable.resolve()
    report = report.absolute()
    staged = report.with_name(report.name + ".tmp")
    # Resolve aliases BEFORE clearing stale reports, including hard links.
    reject_aliases([source, executable, report, staged])
    report.unlink(missing_ok=True)
    staged.unlink(missing_ok=True)
    try:
        if not executable.is_file():
            raise QualificationFailure("built Native source qualification executable is missing")
        original = pinned_source(source)
        process = subprocess.run(
            [str(executable), "--quarantined-jspdf-plan", str(source)],
            capture_output=True, text=True, check=False, timeout=90)
        if process.returncode != 0:
            raise QualificationFailure("Native source qualification executable failed: " + process.stderr[-1000:])
        cases = observation(process.stdout)
        if pinned_source(source) != original:
            raise QualificationFailure("source changed during Native observation")
        record = {
            "schema_version": 3, "qualification": "m2-original-device-native-bounded-rgb",
            "source": {"url": SOURCE_URL, "revision": SOURCE_REVISION,
                       "sha256": SOURCE_SHA256, "size_bytes": SOURCE_SIZE,
                       "unchanged_before_after": True},
            "pixel_budget": 1000000, "mapping": "fitted-integer-viewport",
            "dpi_cases": cases, "native_pixel_positive": True,
            "full_rgb_parity_verified": True,
            "rgb_comparison": "exact-off-source-boundaries-bounded-on-boundaries",
            "official_corpus_admitted": False, "public_sdk_backend_changed": False,
        }
        staged.write_bytes((json.dumps(record, indent=2, sort_keys=True) + "\n").encode("utf-8"))
        staged.replace(report)
    except Exception:
        staged.unlink(missing_ok=True)
        report.unlink(missing_ok=True)
        raise


def self_test():
    import os
    metrics = [
        "M2 RGB dpi=72 different=1360 boundary=1459 L1=141465 budget=686913 max=127 red=8000 stroke=1100 outside=492652 clip_white=15464 samples=exact",
        "M2 RGB dpi=96 different=1570 boundary=1944 L1=150771 budget=1209268 max=191 red=14098 stroke=1224 outside=875966 clip_white=27720 samples=exact",
    ]
    valid = "\n".join([metrics[0], EXPECTED_LINES[0], metrics[1], *EXPECTED_LINES[1:]]) + "\n"
    assert len(observation(valid)) == 3
    for bad in ("", valid + "extra\n", valid.replace("native_pixel_positive=true", "native_pixel_positive=false", 1),
                valid.replace("596x842", "595x842"),
                valid.replace("samples=exact", "samples=unchecked", 1),
                valid.replace("different=1360", "different=1460"),
                valid.replace("L1=141465", "L1=686914"),
                valid.replace("clip_white=15464", "clip_white=0"),
                valid.replace("dpi=72 different", "dpi=96 different"),
                valid.replace("max=127", "max=256"),
                "\n".join(reversed(EXPECTED_LINES))):
        try:
            observation(bad)
        except QualificationFailure:
            pass
        else:
            raise AssertionError("forged or partial Native report accepted")
    with tempfile.TemporaryDirectory(prefix="qp-m2-source-guard-") as temporary:
        root = Path(temporary)
        source, executable, report = [root / x for x in ("source.pdf", "built-test", "report.json")]
        source.write_bytes(b"%PDF-1.3\nnot-the-original")
        executable.write_bytes(b"not-executed")
        for missing in (True, False):
            report.write_bytes(b"stale-success")
            report.with_name(report.name + ".tmp").write_bytes(b"stale-stage")
            try:
                run(source, root / "missing" if missing else executable, report)
            except QualificationFailure:
                pass
            else:
                raise AssertionError("missing executable or forged source accepted")
            assert not report.exists() and not report.with_name(report.name + ".tmp").exists()
        for protected in (source, executable):
            before = protected.read_bytes()
            for alias in (protected, root / "alias"):
                if alias != protected:
                    os.link(protected, alias)
                try:
                    run(source, executable, alias)
                except QualificationFailure:
                    pass
                else:
                    raise AssertionError("protected input/output alias accepted")
                assert protected.read_bytes() == before
                if alias != protected:
                    alias.unlink()
        staged = report.with_name(report.name + ".tmp")
        os.link(source, staged)
        before = source.read_bytes()
        try:
            run(source, executable, report)
        except QualificationFailure:
            pass
        else:
            raise AssertionError("staging alias to source accepted")
        assert source.read_bytes() == before
    print("Original source guard: provenance, alias and stale-output refusals passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.self_test:
        if any((args.source, args.executable, args.report)):
            parser.error("--self-test takes no paths")
        self_test()
        return 0
    if not all((args.source, args.executable, args.report)):
        parser.error("--source, --executable and --report are required")
    try:
        run(args.source, args.executable, args.report)
    except (QualificationFailure, OSError, subprocess.SubprocessError) as error:
        print("M2 original source observation refused:", error, file=sys.stderr)
        return 1
    print("M2 original device/Native bounded-RGB observation attested:", args.report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
