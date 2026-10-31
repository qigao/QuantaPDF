#!/usr/bin/env python3
"""Offline original-glyf reference; never imports production paths or pixels.

Requires fontTools only for reading the checked-in Roboto fixture. Decimal
quadratic roots (60 digits), exact unhinted font units, and the physical
DPI/72 grid classify nonzero 4x4 coverage. Each glyph blends separately in
PDF order, even inside one Tj. No flattening, cubic conversion or SDK use.
The generated RGBA files are checked in; CI needs no fontTools dependency.
"""
from decimal import Decimal as D, getcontext, ROUND_CEILING
from pathlib import Path
import argparse
import hashlib
from fontTools.ttLib import TTFont
from fontTools.pens.basePen import BasePen

getcontext().prec = 60
ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / 'tests/fixtures/fonts/Roboto-Regular.ttf'
SHA = '56a45233d29f11b4dfb86d248e921939d115778f87325e7ae8cc108383d6664d'

class Pen(BasePen):
    def __init__(self, glyphs, x, y, em):
        super().__init__(glyphs)
        self.x, self.y, self.em = D(x), D(y), D(em)
        self.edges = []
    def point(self, p):
        return self.x + D(p[0])*24/self.em, 64-self.y-D(p[1])*24/self.em
    def _moveTo(self, p):
        self.current = self.first = self.point(p)
    def _lineTo(self, p):
        b = self.point(p)
        self.edges.append((self.current, b))
        self.current = b
    def _qCurveToOne(self, q, p):
        a, q, b = self.current, self.point(q), self.point(p)
        den = a[1]-2*q[1]+b[1]
        t = (a[1]-q[1])/den if den else D(0)
        if 0 < t < 1:
            u = tuple(a[i]+(q[i]-a[i])*t for i in (0, 1))
            v = tuple(q[i]+(b[i]-q[i])*t for i in (0, 1))
            c = tuple(u[i]+(v[i]-u[i])*t for i in (0, 1))
            self.edges.extend(((a, u, c), (c, v, b)))
        else:
            self.edges.append((a, q, b))
        self.current = b
    def _closePath(self):
        self.edges.append((self.current, self.first))
    def _endPath(self):
        raise ValueError('open TrueType contour')


def crossing(edge, y):
    a, b = edge[0], edge[-1]
    if not (a[1] <= y < b[1] or b[1] <= y < a[1]):
        return None
    if len(edge) == 2:
        t = (y-a[1])/(b[1]-a[1])
        x = a[0]+(b[0]-a[0])*t
    else:
        q = edge[1]
        aa, bb, cc = a[1]-2*q[1]+b[1], 2*(q[1]-a[1]), a[1]-y
        if not aa:
            t = -cc/bb
        else:
            disc = bb*bb-4*aa*cc
            roots = ((-bb+disc.sqrt())/(2*aa), (-bb-disc.sqrt())/(2*aa))
            good = [t for t in roots if D('-1e-55') <= t <= 1+D('1e-55')]
            assert len(good) == 1
            t = good[0]
        x = (1-t)**2*a[0]+2*(1-t)*t*q[0]+t*t*b[0]
    return x, 1 if b[1] > a[1] else -1


def fixture(case, font):
    widths = {c: (font['hmtx'][font.getBestCmap()[ord(c)]][0]*1000+
                  font['head'].unitsPerEm//2)//font['head'].unitsPerEm for c in 'Heloé'}
    if case == 'layers':
        shows = [('eo', D(18), D(20), (255, 0, 0)),
                 ('é', D(24), D(20), (0, 0, 255)),
                 ('H', D(28), D(20), (0, 0, 0))]
    elif case == 'fringe':
        shows = [('H', D(122), D(-1), (0, 0, 0))]
    else:
        shows = [('Hello', D(12), D(20), (0, 0, 0))]
    glyphs = font.getGlyphSet()
    for text, x, y, color in shows:
        for i, c in enumerate(text):
            if case == 'tj' and i == 2:
                x -= D('2.88')
            if case == 'tj' and i == 3:
                x += D('2.88')
            pen = Pen(glyphs, x, y, font['head'].unitsPerEm)
            glyphs[font.getBestCmap()[ord(c)]].draw(pen)
            yield pen.edges, color
            x += D(widths[c])*24/1000
            if case == 'layers' and text == 'eo':
                x -= 8  # same-Tj overlap: preserve each original glyph paint


def render(case, dpi, font):
    width, height = (128*dpi+71)//72, (64*dpi+71)//72
    pixels = bytearray([255])*(width*height*4)
    for edges, color in fixture(case, font):
        cover = [0]*(width*height)
        for row in range(height*4):
            y = D(2*row+1)*72/(8*dpi)
            if y >= 64:
                continue
            events = sorted(v for e in edges if (v := crossing(e, y)) is not None)
            winding, event = 0, 0
            for col in range(width*4):
                x = D(2*col+1)*72/(8*dpi)
                while event < len(events) and events[event][0] <= x:
                    winding += events[event][1]
                    event += 1
                if winding and x < 128:
                    cover[(row//4)*width+col//4] += 1
        for i, n in enumerate(cover):
            for ch in range(3):
                pixels[4*i+ch] = (color[ch]*n+pixels[4*i+ch]*(16-n)+8)//16
    return pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    assert hashlib.sha256(FONT.read_bytes()).hexdigest() == SHA
    font = TTFont(FONT)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for case in ('hello', 'layers', 'tj', 'fringe'):
        for dpi in (72, 96, 144):
            data = render(case, dpi, font)
            out = args.output_dir / f'roboto-{case}-{dpi}-m0.rgba'
            out.write_bytes(data)
            print(out.name, len(data), hashlib.sha256(data).hexdigest())

if __name__ == '__main__':
    main()
