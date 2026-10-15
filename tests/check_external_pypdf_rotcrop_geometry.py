#!/usr/bin/env python3
"""Quarantine-only immutable external PDF page-dictionary observer.

Not a general PDF parser. Never claims native conformance; actual typed
Source -> Graph -> Owned IR preflight is a separate CTest per page.
Uses only Python's stdlib and never rewrites/normalizes original bytes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re

EXPECTED_SIZE = 272039
EXPECTED_SHA256 = "cc195eac510b81123de4f55ac9f8185f2120975a7b75a106460b98615737aacd"
EXPECTED_GIT_BLOB = "5686652e59ece0109c624da74edc5502ad746fbc"
SOURCE_URL = (
    "https://github.com/py-pdf/sample-files/blob/"
    "02b8203bf416db3dbdb03438bc8278dfcb0555f3/"
    "027-cropped-rotated-scaled/cropped-rotated-scaled.pdf"
)
EXPECTED = (
    (4, (12, 12, 612, 732), (132, 108, 516, 612), 0, (384, 504)),
    (197, (9, 9, 459, 549), (99, 81, 387, 459), 90, (378, 288)),
    (384, (11, 7, 561, 427), (121, 63, 473, 357), 180, (352, 294)),
    (571, (11, 13, 561, 793), (121, 117, 473, 663), 270, (546, 352)),
)
PAGE_PATTERN = re.compile(
    rb"(?m)^(\d+)\s+0\s+obj\s*<<\s*/Type\s*/Page(?=\s)"
    rb"(.*?)>>\s*endobj", re.DOTALL
)


class VerificationError(ValueError):
    pass


def check(condition, reason):
    if not condition:
        raise VerificationError(reason)


def rectangle(dictionary, key):
    match = re.search(rb"/" + key + rb"\s*\[\s*([^\]]{1,160})\]", dictionary)
    check(match is not None, "missing /" + key.decode("ascii"))
    values = tuple(
        int(value) for value in re.findall(rb"[-+]?\d+", match.group(1))
    )
    check(len(values) == 4, "malformed /" + key.decode("ascii"))
    return values


def inspect_bytes(source):
    check(len(source) == EXPECTED_SIZE, "source byte size changed")
    check(source.startswith(b"%PDF-1.7\n"), "unexpected PDF header")
    check(source.rstrip().endswith(b"%%EOF"), "missing PDF EOF")
    sha256 = hashlib.sha256(source).hexdigest()
    blob = hashlib.sha1(
        b"blob " + str(len(source)).encode("ascii") + b"\0" + source
    ).hexdigest()
    check(sha256 == EXPECTED_SHA256, "original SHA-256 mismatch")
    check(blob == EXPECTED_GIT_BLOB, "original Git blob SHA mismatch")
    check(b"/Producer (pypdf)" in source, "upstream producer identity mismatch")
    check(b"/UserUnit" not in source, "source unexpectedly adds /UserUnit")

    pages = list(PAGE_PATTERN.finditer(source))
    check(len(pages) == 4, "expected exactly 4 raw page dictionaries")
    entries = []
    for idx, (matched, expected) in enumerate(zip(pages, EXPECTED)):
        obj_id = int(matched.group(1))
        body = matched.group(2)
        exp_id, media, crop, rotation, visible = expected
        check(obj_id == exp_id, "wrong page indirect ID")
        check(rectangle(body, b"MediaBox") == media, "MediaBox changed")
        check(rectangle(body, b"CropBox") == crop, "CropBox changed")
        rotate = re.search(rb"/Rotate\s+([-+]?\d+)", body)
        check(rotate is not None, "missing /Rotate")
        check(int(rotate.group(1)) == rotation, "Rotate changed")
        check(b"/Annots [" in body, "expected complex annotated page")
        check(b"/Contents" in body, "missing actual source Contents")
        w = crop[2] - crop[0]
        h = crop[3] - crop[1]
        check(crop[0] >= media[0] and crop[1] >= media[1] and
              crop[2] <= media[2] and crop[3] <= media[3],
              "CropBox escaped MediaBox")
        check(w > 0 and h > 0, "empty CropBox")
        dims = (h, w) if rotation in (90, 270) else (w, h)
        check(dims == visible, "visible rotated page dimensions changed")
        entries.append({
            "page_index": idx,
            "indirect_object_id": obj_id,
            "media_box": list(media),
            "crop_box": list(crop),
            "rotate": rotation,
            "user_unit_entry": None,
            "user_unit_default": 1,
            "visible_dimensions_72dpi": list(visible),
            "has_annotations": True,
            "original_bytes_immutable": True,
            "native_pixel_parity_qualified": False,
        })

    check(source.count(b"/Subtype /Image") >= 1, "missing image XObject")
    check(b"/SMask" in source, "missing unsupported image soft mask")
    check(b"/ExtGState" in source, "missing nonopaque state boundary")
    return {
        "schema_version": 1,
        "source_kind": "independently_published_original_quarantine",
        "source_url": SOURCE_URL,
        "upstream_revision": "02b8203bf416db3dbdb03438bc8278dfcb0555f3",
        "size_bytes": len(source),
        "sha256": sha256,
        "git_blob_sha1": blob,
        "license_spdx": "CC-BY-SA-4.0",
        "license_review_issue": "https://github.com/qigao/QuantaPDF/issues/336",
        "license_approved_for_formal_admission": False,
        "observation_only": True,
        "original_pdfium_pixel_oracle": "not_run",
        "native_raster": "not_run",
        "external_native_positive_increment": 0,
        "page_count": 4,
        "pages": entries,
        "unsupported_resource_classes_present": [
            "image_xobject", "image_smask", "transparency", "text",
            "annotations",
        ],
        "source_has_nonunit_user_unit": False,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--report", required=True)
    args = parser.parse_args()
    source_path = Path(args.source)
    report_path = Path(args.report)
    staged = Path(str(report_path) + ".tmp")
    check(source_path.is_file() and not source_path.is_symlink(),
          "source must be a regular file, not a symlink")
    actual_source = source_path.resolve()
    for destination in (report_path, staged):
        check(destination.resolve() != actual_source,
              "report destination aliases original source bytes")
        check(not destination.is_symlink(), "report symlink disallowed")
        if destination.exists():
            check(not os.path.samefile(destination, source_path),
                  "report hardlink aliases original source")
    original = source_path.read_bytes()
    payload = inspect_bytes(original)

    # Demonstrate this guard is effective, rather than simply trusting the
    # declared checksum on the unmodified fixture.
    corrupted = bytearray(original)
    corrupted[10] ^= 0x01
    try:
        inspect_bytes(bytes(corrupted))
    except VerificationError:
        pass
    else:
        raise VerificationError("mutated original unexpectedly admitted")

    check(source_path.read_bytes() == original,
          "source changed during read-only provenance test")
    staged.write_text(json.dumps(payload, sort_keys=True, indent=2) + "\n",
                      encoding="utf-8")
    os.replace(staged, report_path)
    print(
        "quarantine geometry: 4 independently sourced pages, "
        "CropBox/Rotate pinned, UserUnit absent; "
        "native_pixel_positive=0"
    )


if __name__ == "__main__":
    try:
        main()
    except (OSError, VerificationError) as exc:
        parser_error = f"quarantine external geometry integrity failed: {exc}"
        raise SystemExit(parser_error)
