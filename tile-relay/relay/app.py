"""Tile relay: turns Signal K S-57 ENC vector tiles into a Wio-Terminal-sized RGB332 chart image.

GET /chart?lat=..&lon=..&mpp=..&w=320&h=198[&fmt=p4|png]
  p4  -> w*h/2 bytes, 4 bits per pixel (high nibble first) indexing the 16-colour table in render.py
  png -> same picture as PNG (for a browser)
GET /health
"""
import asyncio, gzip, io, os, time
from collections import OrderedDict
import aiohttp
from aiohttp import web
import mapbox_vector_tile
from . import render

SK_URL = os.environ.get('SK_URL', 'http://localhost:3000').rstrip('/')
CHART_ID = os.environ.get('CHART_ID', 'ENC')
SK_TOKEN = os.environ.get('SK_TOKEN', '')
CACHE_TILES = int(os.environ.get('CACHE_TILES', '96'))
MAX_ZOOM = int(os.environ.get('MAX_ZOOM', '18'))
MIN_ZOOM = int(os.environ.get('MIN_ZOOM', '4'))

_cache = OrderedDict()
_inflight = {}
_render_lock = asyncio.Lock()

async def get_tile(session, key):
    if key in _cache:
        _cache.move_to_end(key); return _cache[key]
    if key in _inflight: return await _inflight[key]
    async def load():
        z, x, y = key
        url = f'{SK_URL}/signalk/v1/api/resources/charts/{CHART_ID}/{z}/{x}/{y}'
        headers = {'Authorization': f'JWT {SK_TOKEN}'} if SK_TOKEN else {}
        try:
            async with session.get(url, headers=headers, timeout=aiohttp.ClientTimeout(total=20)) as r:
                if r.status != 200:
                    data = {}
                else:
                    raw = await r.read()
                    if raw[:2] == b'\x1f\x8b': raw = gzip.decompress(raw)
                    data = await asyncio.get_running_loop().run_in_executor(
                        None, lambda: mapbox_vector_tile.decode(raw, y_coord_down=True))
        except Exception as e:
            print('tile fetch failed', key, e); return {}
        _cache[key] = data
        while len(_cache) > CACHE_TILES: _cache.popitem(last=False)
        return data
    t = asyncio.ensure_future(load()); _inflight[key] = t
    try: return await t
    finally: _inflight.pop(key, None)

async def chart(request):
    q = request.query
    try:
        lat, lon, mpp = float(q['lat']), float(q['lon']), float(q['mpp'])
        w, h = min(int(q.get('w', 320)), 480) // 2 * 2, min(int(q.get('h', 198)), 320)
    except (KeyError, ValueError):
        return web.Response(status=400, text='need lat, lon, mpp (metres per pixel); optional w, h')
    if not (-85 < lat < 85 and -180 <= lon <= 180 and 0.05 < mpp < 20000):
        return web.Response(status=400, text='out of range')
    t0 = time.time()
    z = render.pick_zoom(lat, mpp, MIN_ZOOM, MAX_ZOOM)
    keys = render.tiles_needed(lat, lon, mpp, w, h, z)
    if len(keys) > 12:
        return web.Response(status=400, text='view too large for this zoom')
    session = request.app['session']
    got = await asyncio.gather(*[get_tile(session, k) for k in keys])
    tiles = {k: v for k, v in zip(keys, got) if v}
    loop = asyncio.get_running_loop()
    async with _render_lock:
        img = await loop.run_in_executor(None, render.render, lat, lon, mpp, w, h, tiles)
    if q.get('fmt') == 'png':
        buf = io.BytesIO(); img.convert('RGB').save(buf, 'PNG'); body, ctype = buf.getvalue(), 'image/png'
    else:
        body, ctype = render.pack4(img), 'application/octet-stream'
    return web.Response(body=body, content_type=ctype, headers={
        'X-Zoom': str(z), 'X-Tiles': str(len(keys)), 'X-Render-Ms': str(int((time.time() - t0) * 1000))})

async def health(request):
    return web.json_response({'ok': True, 'upstream': SK_URL, 'chart': CHART_ID, 'cached_tiles': len(_cache)})

async def on_start(app): app['session'] = aiohttp.ClientSession()
async def on_stop(app): await app['session'].close()

def make_app():
    app = web.Application()
    app.add_routes([web.get('/chart', chart), web.get('/health', health)])
    app.on_startup.append(on_start); app.on_cleanup.append(on_stop)
    return app

if __name__ == '__main__':
    web.run_app(make_app(), port=int(os.environ.get('PORT', '8090')))
