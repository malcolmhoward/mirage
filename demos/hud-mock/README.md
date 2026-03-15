# MIRAGE HUD Mock Demo

Runs the MIRAGE HUD against simulated sensor data — no physical hardware required.

Four containers start together:

| Service | What it does |
|---------|-------------|
| `mqtt-broker` | Eclipse Mosquitto 2 — message bus |
| `mock-publisher` | Publishes simulated IMU, GPS, environmental, and system metrics to MQTT at ~1 Hz using the O.A.S.I.S. simulation framework |
| `mock-dawn` | Rule-based reasoning demo — subscribes to sensor data, publishes threshold alerts to `oasis/dawn/reasoning` |
| `mirage` | MIRAGE HUD — reads sensor data from MQTT and renders the display |

## Prerequisites

- Docker and Docker Compose
- A built `mirage:dev` image (see [DOCKER.md](../../docs/DOCKER.md))
- Linux host with an X display (for the MIRAGE window)

## Quick Start

```bash
# Build the mirage:dev image first (from the repo root)
docker build -f Dockerfile.dev -t mirage:dev .

# Start the demo
docker compose -f demos/hud-mock/docker-compose.demo.yaml up --build

# Stop the demo
docker compose -f demos/hud-mock/docker-compose.demo.yaml down
```

Allow X connections from Docker if needed:

```bash
xhost +local:docker
```

## What You Should See

- MIRAGE HUD window opens and displays:
  - **Heading / pitch / roll** — values animate via sine-wave simulation
  - **GPS coordinates** — latitude/longitude/altitude update each second
  - **System metrics** — CPU usage and system temperature
  - **Battery status** — level and time remaining
- `mock-dawn` logs threshold alerts to stdout (e.g. high CO2, low battery)

## Simulated Data

The `mock-publisher` uses `MockSensor` from the
[O.A.S.I.S. simulation framework](https://github.com/malcolmhoward/the-oasis-project-simulation-repo)
to generate time-varying values. All values are synthetic — no real sensors,
no real hardware, no external services.

Published topics and message schemas match MIRAGE's expectations exactly
(sourced from `command_processing.c`):

| Topic | `device` field | Key fields |
|-------|---------------|------------|
| `aura` | `Motion` | `heading`, `pitch`, `roll`, `w`, `x`, `y`, `z` |
| `aura` | `Enviro` | `temp`, `humidity`, `air_quality`, `co2_ppm` |
| `aura` | `GPS` | `latitude`, `longitude`, `altitude`, `satellites` |
| `stat` | `SystemMetrics` | `cpu_usage`, `system_temp`, `memory_usage` |
| `stat` | `BatteryStatus` | `battery_level`, `voltage`, `time_remaining_fmt` |

## mock-dawn

`mock-dawn` is a demo artifact — it applies simple threshold rules to simulate
reasoning output without a real DAWN instance. It is not part of the simulation
framework. For full DAWN integration using the Platform layer (LLM mock, Home
Assistant mock), see the simulation framework repository.

## Troubleshooting

**MIRAGE window does not open:**
Ensure `DISPLAY` is set and X connections from Docker are allowed (`xhost +local:docker`).

**`mock-publisher` exits immediately:**
The broker health check may not have passed yet. The service restarts automatically
on failure — wait a few seconds for the broker to become ready.

**`mirage:dev` image not found:**
Build it first from the repo root: `docker build -f Dockerfile.dev -t mirage:dev .`
