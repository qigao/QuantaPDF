#!/usr/bin/env python3
"""Attest M1 device mapping and preliminary M2 Native-positive rendering on one unchanged jsPDF.

No download, vendoring, bitmap artifact, corpus promotion, or SDK switch.
The supplied executable must be the separately built Native source qualification test.
"""
import argparse
import hashlib
import json
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
    "M2 unchanged jsPDF dpi=72 expected=596x842 plan=admitted Native=positive-repeatable-color-regions; preliminary_pixel_positive=true",
    "M2 unchanged jsPDF dpi=96 expected=794x1123 plan=admitted Native=positive-repeatable-color-regions; preliminary_pixel_positive=true",
    "M2 unchanged jsPDF dpi=144 expected=1191x1684 plan=capacity-rejected Native=not-invoked; preliminary_pixel_positive=false",
]


class QualificationFailure(Exception):
    pass


def pinned_source(path):
    with path.open("rb") as stream:
        value = stream.read(SOURCE_SIZE + 1)
    if len(value) != SOURCE_SIZE or hashlib.sha256(value).hexdigest() != SOURCE_SHA256:
        raise QualificationFailure("source bytes do not match the pinned original")
    return value


def observation(stdout):
    if stdout.splitlines() != EXPECTED_LINES:
        raise QualificationFailure("incomplete or changed M2 device/Native-positive observation")
    # The native test asserts source identity, both Path operations, detached
    # ownership, expected grid, repeat byte identity, visible red/black regions,
    # transparent pixels, and zero-output capacity rejection at 144 DPI.
    return [
        {"dpi": dpi, "grid": [width, height], "plan": "admitted",
         "detached_paths": 2, "detached_clips": 1, "detached_graphics_states": 1,
         "source_and_snapshot_disposed_before_raster": True,
         "native_status": "ok", "native_output_zero": False,
         "native_render_count": 2, "repeat_identical": True,
         "visible_regions": ["opaque-red-fill", "opaque-black-stroke", "transparent-outside"],
         "full_rgb_parity_verified": False}
        for dpi, width, height in ((72, 596, 842), (96, 794, 1123))
    ] + [{"dpi": 144, "expected_grid": [1191, 1684],
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
            "schema_version": 2, "qualification": "m2-original-device-native-preliminary-positive",
            "source": {"url": SOURCE_URL, "revision": SOURCE_REVISION,
                       "sha256": SOURCE_SHA256, "size_bytes": SOURCE_SIZE,
                       "unchanged_before_after": True},
            "pixel_budget": 1000000, "mapping": "fitted-integer-viewport",
            "dpi_cases": cases, "native_pixel_positive_preliminary": True,
            "full_rgb_parity_verified": False,
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
    valid = "\n".join(EXPECTED_LINES) + "\n"
    assert len(observation(valid)) == 3
    for bad in ("", valid + "extra\n", valid.replace("preliminary_pixel_positive=true", "preliminary_pixel_positive=false", 1),
                valid.replace("596x842", "595x842"),
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
    print("M2 original device/Native preliminary-positive observation attested:", args.report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
