#!/usr/bin/env python3
"""Strict test-only validator and grouped residual summary for #379."""
import json
import sys
from collections import defaultdict
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit("usage: check_269_open_stroke_clip_ab_pixels.py REPORT.json")
report_path = Path(sys.argv[1])
def reject_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


try:
    data = json.loads(report_path.read_text(encoding="utf-8"),
                      object_pairs_hook=reject_duplicate_keys)
except ValueError as exc:
    raise SystemExit(f"invalid report JSON: {exc}")
if data.get("reference_render_route") != "quantapdf_render_page":
    raise SystemExit("reference_render_route must identify the public 72-DPI renderer path")
if data.get("reference_4x_render_route") != "quantapdf_render_page_with_options":
    raise SystemExit("reference_4x_render_route must identify the public high-DPI renderer path")
if data.get("reference_is_independent_pdfium_oracle") is not False:
    raise SystemExit("reference must not be misrepresented as an independent PDFium oracle")

pixels = data.get("per_pixel_diagnostic")
if not isinstance(pixels, list) or len(pixels) != 3 * 100 * 50:
    raise SystemExit("per_pixel_diagnostic must contain exactly 15000 pixels")

def independent_prediction(x, y, case_i):
    """Recompute segment geometry independently in Python for checker parity."""
    ax, ay, bx, by = 10.0, 10.0, 40.0, 35.0
    dx, dy = bx - ax, by - ay
    length_squared = dx * dx + dy * dy
    cx, cy = x + 0.5, 50.0 - (y + 0.5)
    vx, vy = cx - ax, cy - ay
    projection = (vx * dx + vy * dy) / length_squared
    clamped = max(0.0, min(1.0, projection))
    distance = ((cx - (ax + clamped * dx)) ** 2 +
                (cy - (ay + clamped * dy)) ** 2) ** 0.5
    endpoint = "before_start" if projection < 0 else (
        "after_end" if projection > 1 else "segment_interior")
    center_inside = (case_i != 2 or
                     (18.0 <= cx <= 35.0 and 15.0 <= cy <= 30.0))
    raw = clipped = 0
    for sy in range(4):
        for sx in range(4):
            px = x + (sx + 0.5) / 4.0
            py = 50.0 - (y + (sy + 0.5) / 4.0)
            ux, uy = px - ax, py - ay
            t = (ux * dx + uy * dy) / length_squared
            if not 0.0 <= t <= 1.0:
                continue
            tx, ty = ax + t * dx, ay + t * dy
            if (px - tx) ** 2 + (py - ty) ** 2 <= 0.25:
                raw += 1
                if case_i != 2 or (18.0 <= px <= 35.0 and 15.0 <= py <= 30.0):
                    clipped += 1
    return raw, clipped, projection, distance, endpoint, center_inside


case_names = ("no_clip", "fullpage_w", "restrictive_w")
case_counts = {name: 0 for name in case_names}
case_masks = {name: set() for name in case_names}
groups = defaultdict(list)
prediction_groups = defaultdict(list)
prediction_mismatch_pixels = 0
highres_metrics = {name: {"changed": 0, "abs_vs_72": 0,
                          "native_errors": 0, "native_abs": 0}
                   for name in case_names}
for idx, p in enumerate(pixels):
    case_i, within = divmod(idx, 5000)
    expected_case = case_names[case_i]
    x_expected, y_expected = within % 100, within // 100
    if p.get("case") != expected_case:
        raise SystemExit(f"pixel {idx}: case/order mismatch")
    x, y = p.get("x"), p.get("y")
    y_band = f"y{(y // 10) * 10}-{(y // 10) * 10 + 9}"
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
    raw, clipped, projection_expected, distance_expected, endpoint_expected, center_expected = independent_prediction(x, y, case_i)
    if (p["sample_hits_4x4_raw"] != raw or p["sample_hits_4x4_after_predicted_clip"] != clipped):
        raise SystemExit(f"{expected_case} ({x},{y}): C++ and Python independent sample predictions disagree")
    if not isinstance(p.get("predicted_clip_center_included"), bool) or p["predicted_clip_center_included"] != center_expected:
        raise SystemExit(f"{expected_case} ({x},{y}): predicted Clip center inclusion mismatch")
    projection = p.get("segment_projection")
    distance = p.get("perpendicular_distance")
    if not isinstance(projection, (int, float)) or not isinstance(distance, (int, float)) or distance < 0:
        raise SystemExit(f"{expected_case} ({x},{y}): invalid geometry values")
    if abs(projection - projection_expected) > 1e-6 or abs(distance - distance_expected) > 1e-6:
        raise SystemExit(f"{expected_case} ({x},{y}): geometry coordinate/distance mismatch")
    if p.get("endpoint_region") != endpoint_expected:
        raise SystemExit(f"{expected_case} ({x},{y}): endpoint-region prediction mismatch")

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
    predicted_alpha = (p["sample_hits_4x4_after_predicted_clip"] * 255 + 8) // 16
    alpha_minus_prediction = alpha - predicted_alpha
    if alpha_minus_prediction:
        prediction_mismatch_pixels += 1
    prediction_groups[(expected_case, y_band,
                       p["sample_hits_4x4_after_predicted_clip"],
                       p["endpoint_region"])].append(alpha_minus_prediction)
    if p.get("coverage_residual_alpha_minus_proxy") != coverage_residual:
        raise SystemExit(f"{expected_case} ({x},{y}): coverage residual mismatch")

    highres = p.get("reference_4x_box_rgb")
    native_minus_highres = p.get("native_minus_4x_box_rgb")
    if not (isinstance(highres, list) and len(highres) == 3 and
            isinstance(native_minus_highres, list) and len(native_minus_highres) == 3):
        raise SystemExit(f"{expected_case} ({x},{y}): malformed 4x box-reference channels")
    if any(not isinstance(c, int) or not 0 <= c <= 255 for c in highres):
        raise SystemExit(f"{expected_case} ({x},{y}): 4x box-reference channel out of range")
    expected_native_minus_highres = [native[c] - highres[c] for c in range(3)]
    if native_minus_highres != expected_native_minus_highres:
        raise SystemExit(f"{expected_case} ({x},{y}): Native-minus-4x residual mismatch")
    if p.get("reference_4x_box_is_resampling_diagnostic") is not True:
        raise SystemExit(f"{expected_case} ({x},{y}): 4x reference must be marked diagnostic")
    metrics = highres_metrics[expected_case]
    if any(reference[c] != highres[c] for c in range(3)):
        metrics["changed"] += 1
    if any(native[c] != highres[c] for c in range(3)):
        metrics["native_errors"] += 1
    metrics["abs_vs_72"] += sum(abs(reference[c] - highres[c]) for c in range(3))
    metrics["native_abs"] += sum(abs(native[c] - highres[c]) for c in range(3))

    mismatch = any(v != 0 for v in residual)
    row = data["cases"][case_i]["error_map"][y]
    if (row[x] == "X") != mismatch:
        raise SystemExit(f"{expected_case} ({x},{y}): pixel data disagrees with mismatch map")
    if clip_state != "restrictive_w":
        clip_relation = "not_restrictive"
    else:
        px_center, py_center = x + 0.5, 50.0 - (y + 0.5)
        edge_distance = min(abs(px_center - 18.0), abs(px_center - 35.0),
                            abs(py_center - 15.0), abs(py_center - 30.0))
        clip_relation = "near_boundary" if edge_distance <= 1.0 else (
            "inside" if p["predicted_clip_center_included"] else "outside")
    d = p["perpendicular_distance"]
    distance_band = "d<0.125" if d < 0.125 else (
        "d<0.25" if d < 0.25 else "d<0.5" if d < 0.5 else (
        "d<1" if d < 1.0 else "d>=1"))
    # Focus the grouped report on actual channel/coverage residual evidence.
    if any(residual) or coverage_residual:
        groups[(expected_case, y_band, p["sample_hits_4x4_raw"],
                distance_band, p["endpoint_region"], clip_relation)].append(coverage_residual)

for case in case_names:
    if case_counts[case] != 5000 or len(case_masks[case]) != 5000:
        raise SystemExit(f"{case}: expected 5000 unique pixels")

for case_i, case in enumerate(case_names):
    recorded = data["cases"][case_i]
    metrics = highres_metrics[case]
    expected_fields = {
        "reference_4x_box_changed_pixels_vs_72dpi": metrics["changed"],
        "reference_4x_box_rgb_abs_delta_vs_72dpi": metrics["abs_vs_72"],
        "native_different_pixels_vs_4x_box": metrics["native_errors"],
        "native_rgb_abs_delta_vs_4x_box": metrics["native_abs"],
    }
    for field, expected in expected_fields.items():
        if recorded.get(field) != expected:
            raise SystemExit(f"{case}: {field} summary disagrees with per-pixel records")
print("Validated 15000 per-pixel records; all channel, residual, mask, geometry-schema and coordinate checks passed.")
print(f"Independent 4x4 geometry versus Native alpha: {prediction_mismatch_pixels}/15000 pixels differ from round(clipped_hits*255/16); this is diagnostic, not a PDFium-AA claim.")
print("4x DPI public-renderer reference box-averaged 4x4 to 100x50; diagnostic only, not direct PDFium or internal coverage. case: changed pixels vs 72 DPI / RGB abs delta / Native mismatches / Native RGB abs delta")
for case in case_names:
    m = highres_metrics[case]
    print(f"{case}: {m['changed']} / {m['abs_vs_72']} / {m['native_errors']} / {m['native_abs']}")
print("Grouped Native alpha minus independently predicted 4x4 alpha; group=case/y-band/clipped-hits/endpoint; n/signed_sum/abs_sum/min/max")
for key in sorted(prediction_groups):
    values = prediction_groups[key]
    print(f"{'/'.join(map(str, key))} n={len(values)} signed_sum={sum(values)} abs_sum={sum(abs(v) for v in values)} min={min(values)} max={max(values)}")
print("Grouped nonzero residuals (Native alpha - approximate 255-red proxy); group=case/y-band/raw-hits/distance-band/endpoint/clip-relation; n/signed_sum/abs_sum/min/max")
for key in sorted(groups):
    values = groups[key]
    print(f"{'/'.join(map(str, key))} n={len(values)} signed_sum={sum(values)} abs_sum={sum(abs(v) for v in values)} min={min(values)} max={max(values)}")
