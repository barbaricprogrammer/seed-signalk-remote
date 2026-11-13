# Tile relay for the Wio Terminal Signal K remote.
# Build:  docker build -t seed-signalk-tile-relay tile-relay
# Run:    docker run -d --restart unless-stopped --network host -e SK_URL=http://localhost:3000 seed-signalk-tile-relay
FROM python:3.12-slim
ENV PYTHONUNBUFFERED=1 PORT=8090 SK_URL=http://localhost:3000
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY relay ./relay
RUN useradd -r -u 10001 relay
USER relay
EXPOSE 8090
HEALTHCHECK --interval=30s --timeout=5s CMD python -c "import urllib.request,os;urllib.request.urlopen('http://127.0.0.1:'+os.environ['PORT']+'/health',timeout=4)"
CMD ["python", "-m", "relay.app"]
