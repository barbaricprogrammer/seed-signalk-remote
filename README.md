# seed-signalk-remote

A Signal K remote for the Seeed Wio Terminal (D51R): chart plotter, GPS satellites, heel/pitch and weather, and debug views, plus Signal K alarms on screen and the buzzer, and a microphone noise level published back to Signal K.

- **A / C**: previous / next view. **B**: change page or option (or dismiss an alarm). Joystick pans the chart.
- Views: chart, GPS sources (B cycles sources), heel / pitch / weather, debug.
- The device asks for read/write access on first connect; approve it in the Signal K admin UI (Security, Access Requests).

## Build

Needs [PlatformIO](https://platformio.org). Edit `src/config.h` (it is committed blank; run `git update-index --skip-worktree src/config.h` afterwards so your WiFi password is never committed), then:

    pio run -t upload

Double-slide the power switch to enter bootloader mode if the upload fails.

## Chart tiles

`tile-relay/` converts Signal K chart tiles into small images the Wio can show. See `tile-relay/README.md`.
