# tile-relay

The Wio Terminal has 192 KB of RAM, far too little to decode Signal K's S-57 ENC vector tiles
(27-250 KB each, gzipped). This small service fetches those tiles from your Signal K server,
draws depth areas, land, coastline, contours, soundings, wrecks/obstructions and marks, and returns a
**320x198 image at 4 bits per pixel (31.7 KB)** that the Wio blits straight onto its chart view.

```
Wio  --GET /chart?lat&lon&mpp-->  tile-relay  --GET /signalk/v1/api/resources/charts/ENC/z/x/y-->  Signal K
```

## Run on the Pi (Docker)

1. **Make the image pullable.** GitHub Actions publishes `ghcr.io/barbaricprogrammer/seed-signalk-remote/tile-relay` for amd64 and arm64
   (`.github/workflows/tile-relay-image.yml`). New packages start private: on GitHub open the package's settings and set visibility to
   Public, or run `docker login ghcr.io` on the Pi.
2. **Install Docker** on the Pi if needed: `curl -fsSL https://get.docker.com | sh`, then `sudo usermod -aG docker $USER` and log in again.
3. **Start the relay** (copy `docker-compose.yml` to the Pi, or `git clone` this repo):

   ```bash
   cd tile-relay
   docker compose up -d
   ```

   Or without compose:

   ```bash
   docker run -d --name tile-relay --restart unless-stopped --network host \
     -e SK_URL=http://localhost:3000 \
     ghcr.io/barbaricprogrammer/seed-signalk-remote/tile-relay:latest
   ```

4. **Check it:** `curl http://<pi>:8090/health`, or open `http://<pi>:8090/chart?lat=39.33&lon=-76.405&mpp=8&fmt=png` in a browser.
5. **Point the Wio at it:** set `RELAY_HOST` to the Pi's address in `src/config.h` and reflash. The Debug view's "Tile relay" line shows its status.
6. **Update later:** `docker compose pull && docker compose up -d`. Logs: `docker logs -f tile-relay`.

If Signal K needs a token for chart resources, pass a read-only token as `SK_TOKEN` through the environment (a `.env` file next to the compose file; never commit it).

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
