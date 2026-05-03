"""
mock_dawn.py — Rule-based reasoning demo for the HUD mock demo.

Subscribes to the aura topic and applies simple threshold rules to simulate
DAWN-style reasoning output. Publishes alerts and status to oasis/dawn/reasoning
so that the demo illustrates the full sense → reason → observe loop without
requiring a real DAWN instance.

This is a demo artifact, not a simulation framework component. It is intentionally
simple — no LLM, no intent pipeline. For real DAWN integration, see the simulation
framework's Platform layer (layer2/llm_mock.py).

Configuration (environment variables):
  MQTT_BROKER      broker hostname (default: localhost)
  MQTT_PORT        broker port     (default: 1883)
  AURA_TOPIC       topic to subscribe to (default: aura)
  REASONING_TOPIC  topic to publish reasoning output to (default: oasis/dawn/reasoning)
"""

import json
import os
import time

import paho.mqtt.client as mqtt

BROKER          = os.environ.get("MQTT_BROKER", "localhost")
PORT            = int(os.environ.get("MQTT_PORT", "1883"))
AURA_TOPIC      = os.environ.get("AURA_TOPIC", "aura")
REASONING_TOPIC = os.environ.get("REASONING_TOPIC", "oasis/dawn/reasoning")


def evaluate(payload: dict) -> str | None:
    """Apply threshold rules and return a reasoning string, or None."""
    device = payload.get("device")

    if device == "Motion":
        pitch = payload.get("pitch", 0.0)
        if abs(pitch) > 30:
            return f"High pitch angle detected ({pitch:.1f}°) — check helmet orientation"

    elif device == "Enviro":
        co2 = payload.get("co2_ppm", 0.0)
        temp = payload.get("temp", 0.0)
        if co2 > 1000:
            return f"Elevated CO2 ({co2:.0f} ppm) — consider ventilation"
        if temp > 35:
            return f"High ambient temperature ({temp:.1f}°C) — thermal alert"

    elif device == "GPS":
        satellites = payload.get("satellites", 0)
        if satellites < 4:
            return f"Low GPS satellite count ({satellites}) — positioning degraded"

    elif device == "SystemMetrics":
        cpu = payload.get("cpu_usage", 0.0)
        temp = payload.get("system_temp", 0.0)
        if cpu > 85:
            return f"High CPU usage ({cpu:.1f}%) — system under load"
        if temp > 75:
            return f"High system temperature ({temp:.1f}°C) — thermal throttling risk"

    elif device == "BatteryStatus":
        level = payload.get("battery_level", 100.0)
        if level < 20:
            return f"Low battery ({level:.0f}%) — recharge recommended"

    return None


def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
    except (json.JSONDecodeError, UnicodeDecodeError):
        return

    reasoning = evaluate(payload)
    if reasoning:
        output = {
            "source":    "mock_dawn",
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "device":    payload.get("device", "unknown"),
            "reasoning": reasoning,
        }
        client.publish(REASONING_TOPIC, json.dumps(output))
        print(f"[{output['timestamp']}] {reasoning}")


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        client.subscribe(AURA_TOPIC)
        print(f"Connected — subscribed to {AURA_TOPIC}")
    else:
        print(f"Connection failed: rc={rc}")


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER, PORT, keepalive=60)

print(f"mock_dawn connecting to {BROKER}:{PORT}")
client.loop_forever()
