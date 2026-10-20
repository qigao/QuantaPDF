#!/usr/bin/env python3
"""Independent geometry-only checks for the experimental #379 area model.

These tests validate polygon clipping and pixel-area bookkeeping, not PDFium's
rasterization semantics and not production renderer behavior.  Keep the model
separate from the single-fixture analyzer so held-out geometry can expose
coordinate, clipping, and conservation mistakes before comparing renderers.
"""
from __future__ import annotations

import math
import unittest

Point = tuple[float, float]
Polygon = list[Point]
Rect = tuple[float, float, float, float]


def clip_half_plane(poly: Polygon, axis: int, bound: float,
                    keep_greater: bool) -> Polygon:
    if not poly:
        return []
    out: Polygon = []
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
                out.append((prev[0] + t * (cur[0] - prev[0]),
                            prev[1] + t * (cur[1] - prev[1])))
        if cur_in:
            out.append(cur)
        prev, prev_v, prev_in = cur, cur_v, cur_in
    return out


def clip_rect(poly: Polygon, rect: Rect) -> Polygon:
    xmin, ymin, xmax, ymax = rect
    if not (xmin <= xmax and ymin <= ymax):
        raise ValueError("invalid rectangle")
    for axis, bound, greater in ((0, xmin, True), (0, xmax, False),
                                 (1, ymin, True), (1, ymax, False)):
        poly = clip_half_plane(poly, axis, bound, greater)
    return poly


def polygon_area(poly: Polygon) -> float:
    if len(poly) < 3:
        return 0.0
    return abs(sum(poly[i][0] * poly[(i + 1) % len(poly)][1] -
                   poly[(i + 1) % len(poly)][0] * poly[i][1]
                   for i in range(len(poly))) * 0.5)


def stroke_polygon(a: Point, b: Point, width: float) -> Polygon:
    if not all(math.isfinite(v) for v in (*a, *b, width)):
        raise ValueError("geometry must be finite")
    if width <= 0.0:
        raise ValueError("stroke width must be positive")
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    if length == 0.0:
        raise ValueError("zero-length segment is outside this butt-stroke model")
    nx, ny = -dy * width / (2.0 * length), dx * width / (2.0 * length)
    return [(a[0] + nx, a[1] + ny), (b[0] + nx, b[1] + ny),
            (b[0] - nx, b[1] - ny), (a[0] - nx, a[1] - ny)]


def pixel_coverage(poly: Polygon, x: int, y: int,
                   clip: Rect | None = None) -> float:
    # Coordinates are bottom-left origin; each pixel is [x,x+1] x [y,y+1].
    clipped = clip_rect(poly, (float(x), float(y), float(x + 1), float(y + 1)))
    if clip is not None:
        clipped = clip_rect(clipped, clip)
    return min(1.0, max(0.0, polygon_area(clipped)))


def raster_area_sum(poly: Polygon, bounds: Rect,
                    clip: Rect | None = None) -> float:
    xmin, ymin, xmax, ymax = bounds
    if any(float(v).is_integer() is False for v in bounds):
        raise ValueError("raster bounds must be integer aligned")
    return sum(pixel_coverage(poly, x, y, clip)
               for y in range(int(ymin), int(ymax))
               for x in range(int(xmin), int(xmax)))


class AnalyticCoverageGeometryTests(unittest.TestCase):
    def assertAlmostEqualArea(self, a: float, b: float, places: int = 9) -> None:
        self.assertAlmostEqual(a, b, places=places)

    def test_unclipped_area_conserves_rectangle_for_held_out_slopes(self) -> None:
        # For a butt-ended rectangle, exact area is length * width.
        cases = [
            ((4.25, 5.5), (22.75, 8.125), 0.75),
            ((5.125, 4.25), (9.5, 24.75), 1.25),
            ((3.375, 3.625), (24.125, 23.875), 2.0),
            ((21.75, 22.25), (5.25, 7.125), 0.5),
        ]
        bounds = (0, 0, 32, 32)
        for a, b, width in cases:
            with self.subTest(a=a, b=b, width=width):
                poly = stroke_polygon(a, b, width)
                expected = math.hypot(b[0] - a[0], b[1] - a[1]) * width
                self.assertAlmostEqualArea(polygon_area(poly), expected)
                self.assertAlmostEqualArea(raster_area_sum(poly, bounds), expected)

    def test_pixel_coverage_is_bounded_and_clipping_is_monotone(self) -> None:
        cases = [
            ((2.25, 4.5), (19.75, 12.125), 0.8,
             (7.0, 7.0, 13.0, 11.0)),
            ((4.5, 2.25), (11.125, 20.75), 1.5,
             (5.0, 8.0, 10.0, 17.0)),
            ((2.5, 2.5), (22.5, 22.5), 2.25,
             (9.25, 8.75, 14.5, 15.25)),
        ]
        for a, b, width, clip in cases:
            poly = stroke_polygon(a, b, width)
            for y in range(32):
                for x in range(32):
                    raw = pixel_coverage(poly, x, y)
                    clipped = pixel_coverage(poly, x, y, clip)
                    self.assertGreaterEqual(raw, 0.0)
                    self.assertLessEqual(raw, 1.0)
                    self.assertGreaterEqual(clipped, 0.0)
                    self.assertLessEqual(clipped, raw + 1e-12)

    def test_clip_area_conserves_across_pixel_partition(self) -> None:
        cases = [
            ((3.125, 4.25), (24.5, 17.875), 1.0,
             (8.3, 6.7, 17.2, 14.6)),
            ((6.5, 3.125), (13.875, 26.25), 1.75,
             (7.125, 10.5, 15.25, 22.125)),
            ((26.0, 24.0), (4.0, 8.0), 0.625,
             (8.25, 9.75, 20.5, 20.25)),
        ]
        bounds = (0, 0, 32, 32)
        for a, b, width, clip in cases:
            with self.subTest(a=a, b=b, width=width, clip=clip):
                poly = stroke_polygon(a, b, width)
                expected = polygon_area(clip_rect(poly, clip))
                actual = raster_area_sum(poly, bounds, clip)
                self.assertAlmostEqualArea(actual, expected)

    def test_reversing_segment_preserves_coverage(self) -> None:
        a, b, width = (3.375, 6.125), (25.25, 19.875), 1.375
        forward = stroke_polygon(a, b, width)
        reverse = stroke_polygon(b, a, width)
        self.assertAlmostEqualArea(polygon_area(forward), polygon_area(reverse))
        for y in range(32):
            for x in range(32):
                self.assertAlmostEqualArea(pixel_coverage(forward, x, y),
                                           pixel_coverage(reverse, x, y))

    def test_integer_translation_preserves_pixel_coverage(self) -> None:
        a, b, width = (3.25, 4.75), (17.875, 13.125), 0.875
        dx, dy = 5, 7
        original = stroke_polygon(a, b, width)
        translated = stroke_polygon((a[0] + dx, a[1] + dy),
                                    (b[0] + dx, b[1] + dy), width)
        for y in range(24):
            for x in range(24):
                self.assertAlmostEqualArea(
                    pixel_coverage(original, x, y),
                    pixel_coverage(translated, x + dx, y + dy))

    def test_invalid_or_unsupported_geometry_fails_closed(self) -> None:
        for a, b, width in [
            ((0.0, 0.0), (1.0, 1.0), 0.0),
            ((0.0, 0.0), (1.0, 1.0), -1.0),
            ((0.0, 0.0), (0.0, 0.0), 1.0),
            ((0.0, 0.0), (float("nan"), 1.0), 1.0),
            ((0.0, 0.0), (1.0, 1.0), float("inf")),
        ]:
            with self.subTest(a=a, b=b, width=width):
                with self.assertRaises(ValueError):
                    stroke_polygon(a, b, width)

    def test_invalid_clip_rectangle_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            clip_rect([(0.0, 0.0), (2.0, 0.0), (2.0, 2.0)], (3, 0, 1, 2))


if __name__ == "__main__":
    unittest.main(verbosity=2)
