// Local settings. This committed copy is deliberately blank: fill it in on your machine.
// After editing, stop git from picking up your secrets:  git update-index --skip-worktree src/config.h
#pragma once
#define WIFI_SSID      ""
#define WIFI_PASS      ""
#define SK_HOST        ""          // Signal K server address, e.g. "192.168.1.10"
#define SK_PORT        3000

// Chart centre used until the first GPS fix arrives.
#define DEFAULT_LAT    39.330      // Bowleys Marina (approx.)
#define DEFAULT_LON   -76.405

// Sensor mounting corrections (degrees) for the heel / pitch display.
#define ROLL_OFFSET_DEG   0.0f
#define PITCH_OFFSET_DEG  0.0f

// Tile relay (see tile-relay/README.md). Leave RELAY_HOST empty for the plain grid chart.
#define RELAY_HOST  ""
#define RELAY_PORT  8090
