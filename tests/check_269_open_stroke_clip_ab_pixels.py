#!/usr/bin/env python3
"""Strict test-only validator and grouped residual summary for #379."""
import json
import sys
from collections import defaultdict
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit("usage: check_269_open_stroke_clip_ab_pixels.py REPORT.json")
report_path = Path(sys.argv[1])
data = json.loads(report_path.read_text(encoding="utf-8"))
pixels = data.get("per_pixel_diagnostic")
if not isinstance(pixels, list) or len(pixels) != 3 * 100 * 50:
    raise SystemExit("per_pixel_diagnostic must contain exactly 15000 pixels")

case_names = ("no_clip", "fullpage_w", "restrictive_w")
case_counts = {name: 0 for name in case_names}
case_masks = {name: set() for name in case_names}
groups = defaultdict(list)
for idx, p in enumerate(pixels):
    case_i, within = divmod(idx, 5000)
    expected_case = case_names[case_i]
    x_expected, y_expected = within % 100, within // 100
    if p.get("case") != expected_case:
        raise SystemExit(f"pixel {idx}: case/order mismatch")
    x, y = p.get("x"), p.get("y")
    if (x, y) != (x_expected, y_expected):
        raise SystemExit(f"pixel {idx}: missing, duplicate, or out-of-order coordinate")
    case_counts[expected_case] += 1
    case_masks[expected_case].add((x, y))

    for key in ("sample_hits_4x4_raw", "sample_hits_4x4_after_predicted_clip"):
        v = p.get(key)
        if not isinstance(v, int) or not 0 <= v <= 16:
            raise SystemExit(f"{expected_case} ({x},{y}): invalid {key}")
    if p["sample_hits_4x4_after_predicted_clip"] > p["sample_hits_4x4_raw"]:
        raise SystemExit(f"{expected_case} ({x},{y}): clipped hits exceed raw hits")

    clip_state = p.get("clip_state")
    if clip_state != ("none" if case_i == 0 else "fullpage_w" if case_i == 1 else "restrictive_w"):
        raise SystemExit(f"{expected_case} ({x},{y}): invalid clip_state")
    if not isinstance(p.get("predicted_clip_center_included"), bool):
        raise SystemExit(f"{expected_case} ({x},{y}): clip inclusion must be boolean")
    projection = p.get("segment_projection")
    distance = p.get("perpendicular_distance")
    if not isinstance(projection, (int, float)) or not isinstance(distance, (int, float)) or distance < 0:
        raise SystemExit(f"{expected_case} ({x},{y}): invalid geometry values")
    if p.get("endpoint_region") not in ("before_start", "segment_interior", "after_end"):
        raise SystemExit(f"{expected_case} ({x},{y}): invalid endpoint region")

    rgba, native, reference = (p.get("native_rgba"), p.get("native_rgb_on_white"), p.get("reference_rgb"))
    if not (isinstance(rgba, list) and len(rgba) == 4 and
            isinstance(native, list) and len(native) == 3 and
            isinstance(reference, list) and len(reference) == 3):
        raise SystemExit(f"{expected_case} ({x},{y}): malformed pixel channel arrays")
    if any(not isinstance(c, int) or not 0 <= c <= 255 for c in rgba + native + reference):
        raise SystemExit(f"{expected_case} ({x},{y}): channel out of range")
    alpha = rgba[3]
    if not any(alpha == (n * 255 + 8) // 16 for n in range(17)):
        raise SystemExit(f"{expected_case} ({x},{y}): alpha off 4x4 quantization lattice")
    composited = [(rgba[c] * alpha + 255 * (255 - alpha) + 127) // 255 for c in range(3)]
    if composited != native:
        raise SystemExit(f"{expected_case} ({x},{y}): Native-over-white RGB mismatch")
    residual = p.get("signed_rgb_residual")
    if not isinstance(residual, list) or len(residual) != 3 or residual != [native[c] - reference[c] for c in range(3)]:
        raise SystemExit(f"{expected_case} ({x},{y}): signed RGB residual mismatch")
    if p.get("coverage_proxy_approximate") is not True:
        raise SystemExit(f"{expected_case} ({x},{y}): coverage proxy must be marked approximate")
    proxy = 255 - reference[0]
    if p.get("coverage_proxy_255_minus_reference_red") != proxy:
        raise SystemExit(f"{expected_case} ({x},{y}): coverage proxy mismatch")
    coverage_residual = alpha - proxy
    if p.get("coverage_residual_alpha_minus_proxy") != coverage_residual:
        raise SystemExit(f"{expected_case} ({x},{y}): coverage residual mismatch")

    mismatch = any(v != 0 for v in residual)
    row = data["cases"][case_i]["error_map"][y]
    if (row[x] == "X") != mismatch:
        raise SystemExit(f"{expected_case} ({x},{y}): pixel data disagrees with mismatch map")
    groups[(expected_case, p["sample_hits_4x4_raw"], p["endpoint_region"])].append(coverage_residual)

for case in case_names:
    if case_counts[case] != 5000 or len(case_masks[case]) != 5000:
        raise SystemExit(f"{case}: expected 5000 unique pixels")

print("Validated 15000 per-pixel records; all channel, residual, mask, geometry-schema and coordinate checks passed.")
print("Grouped coverage residual (Native alpha - approximate 255-red proxy); group=case/raw 4x4 hits/endpoint; n/signed_sum/abs_sum/min/max")
for key in sorted(groups):
    values = groups[key]
    print(f"{key[0]}/{key[1]}/{key[2]} n={len(values)} signed_sum={sum(values)} abs_sum={sum(abs(v) for v in values)} min={min(values)} max={max(values)}")
