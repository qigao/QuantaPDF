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
import zlib
from fontTools.ttLib import TTFont
from fontTools.pens.basePen import BasePen

getcontext().prec = 60
ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / 'tests/fixtures/fonts/Roboto-Regular.ttf'
SHA = '56a45233d29f11b4dfb86d248e921939d115778f87325e7ae8cc108383d6664d'

class Pen(BasePen):
    def __init__(self, glyphs, x, y, em, height=64):
        super().__init__(glyphs)
        self.x, self.y, self.em = D(x), D(y), D(em)
        self.height = D(height)
        self.edges = []
    def point(self, p):
        return self.x + D(p[0])*24/self.em, self.height-self.y-D(p[1])*24/self.em
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


def fixture(case, font, height=64):
    def rect(x,y,w,h,color):
        x,y,w,h=map(D,(x,y,w,h));a=(x,height-y);b=(x+w,height-y);c=(x+w,height-y-h);d=(x,height-y-h)
        return [(a,b),(b,c),(c,d),(d,a)],color
    if case in ('decimal', 'decimal-shapes'):
        if case == 'decimal':
            yield from fixture('hello', font, height)
        yield rect('20','16','12','12',(153,51,204))
        yield rect('12','20','7','10',(0,255,0))
        if case == 'decimal-shapes':
            yield rect('14','22','12','8',(0,0,255))
        return
    if case in ('mixed','reverse','background'):
        yield rect('0','0','128','64',(223,223,223))
        if case=='reverse':yield rect('25.125','18.25','12.5','9.5',(0,0,255))
        yield from fixture('hello',font)
        if case!='background':
            if case=='mixed':yield rect('25.125','18.25','12.5','9.5',(0,0,255))
            glyphs=font.getGlyphSet();pen=Pen(glyphs,D(24),D(20),font['head'].unitsPerEm)
            glyphs[font.getBestCmap()[233]].draw(pen);yield pen.edges,(255,0,0)
        return
    if case=='late':
        yield from fixture('hello',font);yield rect('1','1','2','2',(0,0,0));return
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
            pen = Pen(glyphs, x, y, font['head'].unitsPerEm, height)
            glyphs[font.getBestCmap()[ord(c)]].draw(pen)
            yield pen.edges, color
            x += D(widths[c])*24/1000
            if case == 'layers' and text == 'eo':
                x -= 8  # same-Tj overlap: preserve each original glyph paint


def turned_edges(edges, rotation, width_points=128, height_points=64):
    """Rotate original quadratic coordinates before solving scanline roots."""
    def point(p):
        x, y = p
        return {0: (x, y), 90: (height_points-y, x),
                180: (width_points-x, height_points-y), 270: (y, width_points-x)}[rotation]
    for edge in edges:
        transformed = tuple(map(point, edge))
        if len(edge) == 2:
            yield transformed
            continue
        a, q, b = transformed
        den = a[1]-2*q[1]+b[1]
        t = (a[1]-q[1])/den if den else D(0)
        if 0 < t < 1:
            u = tuple(a[i]+(q[i]-a[i])*t for i in (0, 1))
            v = tuple(q[i]+(b[i]-q[i])*t for i in (0, 1))
            c = tuple(u[i]+(v[i]-u[i])*t for i in (0, 1))
            yield a, u, c
            yield c, v, b
        else:
            yield transformed


def sample_mask(edges, dpi, width_points=128, height_points=64):
    """One original glyph's exact 4x4 membership, before alpha rounding."""
    width = int((D(width_points)*dpi/72).to_integral_value(rounding=ROUND_CEILING))
    height = int((D(height_points)*dpi/72).to_integral_value(rounding=ROUND_CEILING))
    masks = [0]*(width*height)
    for row in range(height*4):
        y = D(2*row+1)*72/(8*dpi)
        if y >= height_points:
            continue
        events = sorted(v for e in edges if (v := crossing(e, y)) is not None)
        winding, event = 0, 0
        for col in range(width*4):
            x = D(2*col+1)*72/(8*dpi)
            while event < len(events) and events[event][0] <= x:
                winding += events[event][1]
                event += 1
            if winding and x < width_points:
                masks[(row//4)*width+col//4] |= 1 << (4*(row%4)+col%4)
    return masks


def render(case, dpi, font, rotation=0):
    original_w, original_h = (D('237.133'), D('180.44')) if case.startswith('decimal') else (D(128), D(64))
    wp, hp = (original_h, original_w) if rotation in (90, 270) else (original_w, original_h)
    width = int((wp*dpi/72).to_integral_value(rounding=ROUND_CEILING))
    height = int((hp*dpi/72).to_integral_value(rounding=ROUND_CEILING))
    pixels = bytearray([255])*(width*height*4)
    for edges, color in fixture(case, font, original_h):
        oriented = list(turned_edges(edges, rotation, original_w, original_h)) if rotation else edges
        for i, mask in enumerate(sample_mask(oriented, dpi, wp, hp)):
            n = mask.bit_count()
            for ch in range(3):
                pixels[4*i+ch] = (color[ch]*n+pixels[4*i+ch]*(16-n)+8)//16
    return pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    cases = ('hello', 'layers', 'tj', 'fringe', 'mixed', 'reverse', 'background', 'late')
    parser.add_argument('--case', action='append', choices=cases+('decimal','decimal-shapes'))
    parser.add_argument('--dpi', type=int, action='append', choices=(72, 96, 144))
    parser.add_argument('--rotation', type=int, default=0, choices=(0, 90, 180, 270))
    parser.add_argument('--compress', action='store_true', help='write lossless zlib-compressed RGBA with .z suffix')
    parser.add_argument('--sample-masks', action='store_true',
                        help='also write five Hello glyph layers as little-endian uint16 masks')
    args = parser.parse_args()
    if args.sample_masks and args.rotation:
        parser.error('--sample-masks requires the original zero rotation')
    assert hashlib.sha256(FONT.read_bytes()).hexdigest() == SHA
    font = TTFont(FONT)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for case in args.case or cases:
        for dpi in args.dpi or (72, 96, 144):
            data = render(case, dpi, font, args.rotation)
            suffix = f'-rotate{args.rotation}' if args.rotation else ''
            out = args.output_dir / f'roboto-{case}-{dpi}-m{1 if case in ("mixed","reverse","background","late") else 0}{suffix}.rgba'
            if args.compress:
                data = zlib.compress(data, 9)
                out = out.with_suffix('.rgba.z')
            out.write_bytes(data)
            print(out.name, len(data), hashlib.sha256(data).hexdigest())
    if args.sample_masks:
        for dpi in args.dpi or (72, 96, 144):
            layers = [sample_mask(edges, dpi) for edges, _ in fixture('hello', font)]
            assert len(layers) == 5
            data = b''.join(mask.to_bytes(2, 'little') for layer in layers for mask in layer)
            out = args.output_dir / f'roboto-hello-{dpi}-m2b2i.mask16'
            out.write_bytes(data)
            print(out.name, len(data), hashlib.sha256(data).hexdigest())

if __name__ == '__main__':
    main()
