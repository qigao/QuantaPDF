#!/usr/bin/env python3
"""Offline analytic pixel-area candidate for the #379 open-line diagnostic.

This is deliberately a test-side experiment, not a renderer implementation or
claim about PDFium's normative rasterization semantics. It handles only the
known 1pt butt-ended segment and optional axis-aligned rectangular W clip.
"""
import json
import sys
from pathlib import Path

W, H = 100, 50
A, B, HALF_WIDTH = (10.0, 10.0), (40.0, 35.0), 0.5
CLIP = (18.0, 15.0, 35.0, 30.0)


def clip_half_plane(poly, axis, bound, keep_greater):
    if not poly:
        return []
    out = []
    prev = poly[-1]
    prev_v = prev[axis]
    prev_in = prev_v >= bound if keep_greater else prev_v <= bound
    for cur in poly:
        cur_v = cur[axis]
        cur_in = cur_v >= bound if keep_greater else cur_v <= bound
        if cur_in != prev_in:
            denom = cur_v - prev_v
            if denom != 0.0:
                t = (bound - prev_v) / denom
                p = (prev[0] + t * (cur[0] - prev[0]),
                     prev[1] + t * (cur[1] - prev[1]))
                out.append(p)
        if cur_in:
            out.append(cur)
        prev, prev_v, prev_in = cur, cur_v, cur_in
    return out


def clip_rect(poly, xmin, ymin, xmax, ymax):
    for axis, bound, greater in ((0, xmin, True), (0, xmax, False),
                                 (1, ymin, True), (1, ymax, False)):
        poly = clip_half_plane(poly, axis, bound, greater)
    return poly


def area(poly):
    if len(poly) < 3:
        return 0.0
    return abs(sum(poly[i][0] * poly[(i + 1) % len(poly)][1] -
                   poly[(i + 1) % len(poly)][0] * poly[i][1]
                   for i in range(len(poly))) * 0.5)


def stroke_polygon():
    ax, ay = A
    bx, by = B
    dx, dy = bx - ax, by - ay
    length = (dx * dx + dy * dy) ** 0.5
    nx, ny = -dy * HALF_WIDTH / length, dx * HALF_WIDTH / length
    return [(ax + nx, ay + ny), (bx + nx, by + ny),
            (bx - nx, by - ny), (ax - nx, ay - ny)]


def pixel_coverage(x, y, restricted):
    # Report coordinates are top-left; convert each pixel to PDF bottom-left.
    box = (float(x), float(H - y - 1), float(x + 1), float(H - y))
    poly = clip_rect(stroke_polygon(), *box)
    if restricted:
        poly = clip_rect(poly, *CLIP)
    return max(0.0, min(1.0, area(poly)))


def main(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    pixels = data.get("per_pixel_diagnostic")
    if not isinstance(pixels, list) or len(pixels) != 3 * W * H:
        raise SystemExit("expected exact-head report with 15000 per-pixel records")
    names = ("no_clip", "fullpage_w", "restrictive_w")
    for ci, name in enumerate(names):
        rows = pixels[ci * W * H:(ci + 1) * W * H]
        active = 0
        pdfium_error = native_error = 0.0
        candidate_abs_rgb = 0
        max_pdfium_error = max_native_error = 0.0
        for p in rows:
            x, y = p.get("x"), p.get("y")
            if p.get("case") != name or (x, y) != (p["x"], p["y"]):
                raise SystemExit(f"{name}: malformed or unordered pixel record")
            cov = pixel_coverage(x, y, ci == 2)
            ref, native = p["reference_rgb"], p["native_rgb_on_white"]
            # Blue stroke on white: red channel is a coverage proxy. The
            # candidate predicts equal R/G and constant blue, not full PDF color semantics.
            ref_cov = (255 - ref[0]) / 255.0
            native_cov = (255 - native[0]) / 255.0
            if cov > 0.0 or ref_cov > 0.0 or p["native_rgba"][3] > 0:
                active += 1
                pdf_err, native_err = abs(ref_cov - cov), abs(native_cov - cov)
                pdfium_error += pdf_err
                native_error += native_err
                max_pdfium_error = max(max_pdfium_error, pdf_err)
                max_native_error = max(max_native_error, native_err)
                candidate_r = round(255 * (1.0 - cov))
                candidate_abs_rgb += abs(candidate_r - ref[0])
        print(f"{name}: active={active}; coverage_MAE_pdfium={pdfium_error / max(active, 1):.8f}; "
              f"coverage_MAE_native={native_error / max(active, 1):.8f}; "
              f"max_pdfium_error={max_pdfium_error:.8f}; max_native_error={max_native_error:.8f}; "
              f"analytic_red_abs_delta={candidate_abs_rgb}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: analyze_269_open_stroke_clip_coverage.py REPORT.json")
    main(sys.argv[1])
