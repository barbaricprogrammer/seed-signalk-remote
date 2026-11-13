"""Rasterise Signal K S-57 ENC vector tiles into a small 8-bit (RGB332) chart image for the Wio Terminal."""
import math
from PIL import Image, ImageDraw, ImageFont

# 16-colour palette (4 bits per pixel on the wire). The Wio has the same table in src/chartpal.h;
# `python3 tile-relay/tools/gen_palette.py` regenerates that header from here.
COLOURS = [
    (200, 225, 255),   #  0 DEEP      > 18 m
    (170, 205, 255),   #  1 D_MID     9 - 18 m
    (140, 180, 240),   #  2 D_SAFE    3.6 - 9 m
    (110, 150, 220),   #  3 D_SHAL    1.8 - 3.6 m
    (80, 120, 190),    #  4 D_VSHAL   0 - 1.8 m
    (150, 190, 120),   #  5 DRYING    intertidal
    (250, 235, 170),   #  6 LAND
    (60, 40, 20),      #  7 LAND_EDGE coastline
    (60, 80, 140),     #  8 CONTOUR   depth contour
    (20, 20, 90),      #  9 TEXT      soundings
    (255, 0, 0),       # 10 DANGER    wrecks / obstructions
    (255, 0, 255),     # 11 MARK      beacons, buoys
    (190, 215, 255),   # 12 DREDGED   dredged areas
]
DEEP, D_MID, D_SAFE, D_SHAL, D_VSHAL, DRYING, LAND, LAND_EDGE, CONTOUR, TEXT, DANGER, MARK, DREDGED = range(13)
PALETTE = [v for rgb in COLOURS for v in rgb] + [0] * (768 - 3 * len(COLOURS))

def pack4(img):
    """Pack a 'P' image (indices 0..15) into 4 bits per pixel, high nibble first. Width must be even."""
    raw = img.tobytes()
    return bytes((a << 4) | b for a, b in zip(raw[0::2], raw[1::2]))

def depth_colour(d1, d2):
    d1 = 0 if d1 is None else d1
    if d1 < 0: return DRYING
    if d1 < 1.8: return D_VSHAL
    if d1 < 3.6: return D_SHAL
    if d1 < 9: return D_SAFE
    if d1 < 18: return D_MID
    return DEEP

FONT = ImageFont.load_default()
TILE = 256.0
M_PER_PX_Z0 = 156543.03392804097   # metres per pixel of a 256-px tile at z0, equator

def pick_zoom(lat, mpp, minzoom=4, maxzoom=18):
    z = math.log2(M_PER_PX_Z0 * math.cos(math.radians(lat)) / max(mpp, 0.01))
    return max(minzoom, min(maxzoom, int(round(z))))

def world_px(lat, lon, z):
    n = TILE * (2 ** z)
    x = (lon + 180.0) / 360.0 * n
    s = math.sin(math.radians(lat))
    y = (0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * n
    return x, y

def _scale(lat, z, mpp):
    return M_PER_PX_Z0 * math.cos(math.radians(lat)) / (2 ** z) / mpp   # output px per world px

def tiles_needed(lat, lon, mpp, w, h, z):
    cx, cy = world_px(lat, lon, z)
    k = _scale(lat, z, mpp)
    hw, hh = (w / 2) / k, (h / 2) / k
    n = 2 ** z
    x0, x1 = int((cx - hw) // TILE), int((cx + hw) // TILE)
    y0, y1 = int((cy - hh) // TILE), int((cy + hh) // TILE)
    return [(z, x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1) if 0 <= x < n and 0 <= y < n]

def _rings(geom):
    t, c = geom['type'], geom['coordinates']
    if t == 'Polygon': c = [c]
    elif t != 'MultiPolygon': return
    for p in c:
        if p: yield p[0], p[1:]

def _lines(geom):
    t, c = geom['type'], geom['coordinates']
    if t == 'LineString': yield c
    elif t == 'MultiLineString':
        for l in c: yield l

def _points(geom):
    t, c = geom['type'], geom['coordinates']
    if t == 'Point': yield c
    elif t == 'MultiPoint':
        for p in c: yield p

def render(lat, lon, mpp, w, h, tiles, show_soundings=True):
    """tiles: {(z,x,y): decoded MVT (y down)}. Returns a 'P' image whose raw bytes are RGB332."""
    img = Image.new('P', (w, h), DEEP); img.putpalette(PALETTE)
    d = ImageDraw.Draw(img)
    if not tiles: return img
    z = next(iter(tiles))[0]
    cx, cy = world_px(lat, lon, z)
    k = _scale(lat, z, mpp)

    def tx(tile):
        _, tX, tY = tile
        return lambda p, ext: ((tX * TILE + p[0] * TILE / ext - cx) * k + w / 2,
                               (tY * TILE + p[1] * TILE / ext - cy) * k + h / 2)

    layers = {}
    for tile, data in tiles.items():
        for name, l in data.items():
            layers.setdefault(name, []).append((tile, l))

    def each(name):
        for tile, l in layers.get(name, []):
            yield l['features'], l.get('extent', 4096), tx(tile)

    def polys(name, colour_fn):
        for feats, ext, f in each(name):
            for ft in feats:
                col = colour_fn(ft['properties'])
                for ring, holes in _rings(ft['geometry']):
                    pts = [f(p, ext) for p in ring]
                    if len(pts) < 3: continue
                    if not holes:
                        d.polygon(pts, fill=col)
                    else:
                        m = Image.new('L', (w, h), 0); md = ImageDraw.Draw(m)
                        md.polygon(pts, fill=255)
                        for hole in holes:
                            hp = [f(p, ext) for p in hole]
                            if len(hp) >= 3: md.polygon(hp, fill=0)
                        img.paste(col, mask=m)

    def lines(name, col):
        for feats, ext, f in each(name):
            for ft in feats:
                for ln in _lines(ft['geometry']):
                    pts = [f(p, ext) for p in ln]
                    if len(pts) > 1: d.line(pts, fill=col, width=1)

    polys('DEPARE', lambda p: depth_colour(p.get('DRVAL1'), p.get('DRVAL2')))
    polys('DRGARE', lambda p: DREDGED)
    polys('LNDARE', lambda p: LAND)
    lines('DEPCNT', CONTOUR)
    lines('COALNE', LAND_EDGE)
    lines('SLCONS', LAND_EDGE)
    for name in ('OBSTRN', 'WRECKS'):
        for feats, ext, f in each(name):
            for ft in feats:
                for p in _points(ft['geometry']):
                    x, y = f(p, ext)
                    d.line([(x - 3, y - 3), (x + 3, y + 3)], fill=DANGER); d.line([(x - 3, y + 3), (x + 3, y - 3)], fill=DANGER)
    for name in ('BCNLAT', 'BOYLAT', 'BOYCAR', 'BOYSPP', 'DAYMAR', 'PILPNT'):
        for feats, ext, f in each(name):
            for ft in feats:
                for p in _points(ft['geometry']):
                    x, y = f(p, ext); d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=MARK)
    if show_soundings and mpp < 40:
        seen = set()
        for feats, ext, f in each('SOUNDG'):
            for ft in feats:
                dep = ft['properties'].get('DEPTH')
                if dep is None: continue
                for p in _points(ft['geometry']):
                    x, y = f(p, ext)
                    if not (0 <= x < w and 0 <= y < h): continue
                    key = (int(x) // 18, int(y) // 10)
                    if key in seen: continue
                    seen.add(key)
                    d.text((x - 4, y - 4), ('%d' % dep) if dep >= 10 else ('%.1f' % dep), fill=TEXT, font=FONT)
    return img
