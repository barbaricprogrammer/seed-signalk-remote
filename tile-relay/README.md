# tile-relay

The Wio Terminal has 192 KB of RAM, far too little to decode Signal K's S-57 ENC vector tiles
(27-250 KB each, gzipped). This small service fetches those tiles from your Signal K server,
draws depth areas, land, coastline, contours, soundings, wrecks/obstructions and marks, and returns a
**320x198 image at 4 bits per pixel (31.7 KB)** that the Wio blits straight onto its chart view.

```
Wio  --GET /chart?lat&lon&mpp-->  tile-relay  --GET /signalk/v1/api/resources/charts/ENC/z/x/y-->  Signal K
```

## Run on the Pi (Docker)

```bash
docker run -d --name tile-relay --restart unless-stopped --network host \
  -e SK_URL=http://localhost:3000 \
  ghcr.io/barbaricprogrammer/seed-signalk-remote/tile-relay:latest
```

The image is built for amd64 and arm64 by `.github/workflows/tile-relay-image.yml`.
Set `RELAY_HOST` / `RELAY_PORT` (default 8090) in the firmware's `src/config.h` to the Pi.

## Run without Docker

```bash
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
SK_URL=http://10.0.0.227:3000 .venv/bin/python -m relay.app
```

## API

`GET /chart?lat=39.33&lon=-76.405&mpp=8[&w=320&h=198][&fmt=p4|png]`
- `mpp` is metres per pixel. `fmt=png` renders in a browser; the default `p4` is what the Wio uses.
- `p4`: `w*h/2` bytes, high nibble first, each nibble indexes the 16 colours in `relay/render.py`.
  `src/chartpal.h` holds the same table in RGB565; regenerate it with `python3 tools/gen_palette.py` after changing colours.

`GET /health` - status and cache size.

## Settings (environment)

| Variable | Default | |
|---|---|---|
| `SK_URL` | `http://localhost:3000` | Signal K server |
| `CHART_ID` | `ENC` | chart id under `/resources/charts` |
| `SK_TOKEN` | empty | JWT, only if the server requires auth for resources |
| `PORT` | `8090` | listen port |
| `CACHE_TILES` | `96` | decoded tiles kept in memory |

The relay has no authentication; run it on your boat LAN only.
