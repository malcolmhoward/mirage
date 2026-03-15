"""
mock_publisher.py — Publishes simulated O.A.S.I.S. sensor data to MQTT.

Publishes to the topics MIRAGE subscribes to, using MIRAGE's exact JSON schemas
(sourced from command_processing.c). Values vary over time via the simulation
framework's sine-wave generators so the HUD display animates naturally.

Topics:
  aura  — Motion (IMU), Enviro (environmental), GPS
  stat  — SystemMetrics, BatteryStatus

Configuration (environment variables):
  MQTT_BROKER       broker hostname (default: localhost)
  MQTT_PORT         broker port     (default: 1883)
  AURA_TOPIC        topic for armor sensor data (default: aura)
  STAT_TOPIC        topic for system metrics     (default: stat)
  PUBLISH_INTERVAL  seconds between publishes    (default: 1.0)
"""

import json
import os
import time

import paho.mqtt.client as mqtt

from simulation.layer0.sensor import MockSensor

BROKER   = os.environ.get("MQTT_BROKER", "localhost")
PORT     = int(os.environ.get("MQTT_PORT", "1883"))
AURA     = os.environ.get("AURA_TOPIC", "aura")
STAT     = os.environ.get("STAT_TOPIC", "stat")
INTERVAL = float(os.environ.get("PUBLISH_INTERVAL", "1.0"))

imu    = MockSensor("imu",    sensor_type="motion")
gps    = MockSensor("gps",    sensor_type="gps")
enviro = MockSensor("enviro", sensor_type="environmental")


def motion_payload(reading: dict) -> dict:
    """Map MockSensor motion reading to MIRAGE's expected schema."""
    return {
        "device":  "Motion",
        "format":  "Orientation",
        "heading": round(reading["heading"], 1),
        "pitch":   round(reading["pitch"],   1),
        "roll":    round(reading["roll"],    1),
        "w":       round(reading.get("w", 1.0), 4),
        "x":       round(reading.get("x", 0.0), 4),
        "y":       round(reading.get("y", 0.0), 4),
        "z":       round(reading.get("z", 0.0), 4),
    }


def enviro_payload(reading: dict) -> dict:
    return {
        "device":      "Enviro",
        "temp":        round(reading["temp"],        1),
        "humidity":    round(reading["humidity"],    1),
        "air_quality": round(reading["air_quality"], 1),
        "tvoc_ppb":    round(reading["tvoc_ppb"],    1),
        "eco2_ppm":    round(reading["eco2_ppm"],    1),
        "co2_ppm":     round(reading["co2_ppm"],     1),
        "heat_index_c": round(reading["temp"] + 1.5, 1),
        "dew_point":   round(reading["temp"] - 8.0,  1),
    }


def gps_payload(reading: dict) -> dict:
    lat = reading["latitude"]
    lon = reading["longitude"]
    return {
        "device":           "GPS",
        "time":             time.strftime("%H:%M:%S"),
        "date":             time.strftime("%Y-%m-%d"),
        "fix":              1,
        "latitude":         round(lat, 6),
        "latitudeDegrees":  round(lat, 6),
        "lat":              "N" if lat >= 0 else "S",
        "longitude":        round(lon, 6),
        "longitudeDegrees": round(lon, 6),
        "lon":              "W" if lon <= 0 else "E",
        "speed":            round(reading.get("speed", 0.0), 1),
        "angle":            round(reading.get("angle", 0.0), 1),
        "altitude":         round(reading["altitude"],       1),
        "satellites":       reading["satellites"],
    }


def system_metrics_payload() -> dict:
    import random
    return {
        "device":      "SystemMetrics",
        "cpu_usage":   round(40.0 + 20.0 * abs(time.time() % 10 / 10 - 0.5), 1),
        "system_temp": round(55.0 + 15.0 * abs(time.time() % 20 / 20 - 0.5), 1),
        "memory_usage": round(60.0 + 10.0 * abs(time.time() % 15 / 15 - 0.5), 1),
    }


def battery_payload() -> dict:
    level = 85.0 - (time.time() % 3600) / 3600 * 5  # slowly drains
    return {
        "device":             "BatteryStatus",
        "voltage":            round(12.5 - (100 - level) * 0.02, 2),
        "current":            2.1,
        "power":              round(2.1 * (12.5 - (100 - level) * 0.02), 2),
        "battery_level":      round(level, 1),
        "battery_status":     "discharging",
        "time_remaining_min": round(level * 3.0, 0),
        "time_remaining_fmt": f"{int(level * 3 // 60)}:{int(level * 3 % 60):02d}",
    }


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"Connected to broker {BROKER}:{PORT}")
    else:
        print(f"Connection failed: rc={rc}")


client = mqtt.Client()
client.on_connect = on_connect
client.connect(BROKER, PORT, keepalive=60)
client.loop_start()

print(f"Publishing to {BROKER}:{PORT} — topics: {AURA}, {STAT}")
print(f"Interval: {INTERVAL}s  (Ctrl-C to stop)")

try:
    while True:
        imu_r    = imu.read()
        gps_r    = gps.read()
        enviro_r = enviro.read()

        client.publish(AURA, json.dumps(motion_payload(imu_r)))
        client.publish(AURA, json.dumps(enviro_payload(enviro_r)))
        client.publish(AURA, json.dumps(gps_payload(gps_r)))
        client.publish(STAT, json.dumps(system_metrics_payload()))
        client.publish(STAT, json.dumps(battery_payload()))

        time.sleep(INTERVAL)
except KeyboardInterrupt:
    print("Stopped.")
finally:
    client.loop_stop()
    client.disconnect()
