/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * By contributing to this project, you agree to license your contributions
 * under the GPLv3 (or any later version) or any future licenses chosen by
 * the project author(s). Contributions include any modifications,
 * enhancements, or additions to the project. These contributions become
 * part of the project and are adopted by the project author(s).
 *
 * Component Status (Keepalive) API for Mirage
 *
 * Implements OCP v1.3 Component Status protocol:
 * - Publishes hud/status with LWT and heartbeat
 * - Subscribes to dawn/status for Dawn presence
 * - Provides Dawn online status for HUD indicators
 */

#ifndef COMPONENT_STATUS_H
#define COMPONENT_STATUS_H

#include <mosquitto.h>
#include <stdbool.h>

/* Status topic definitions (OCP v1.3) */
#define STATUS_TOPIC_HUD "hud/status"
#define STATUS_TOPIC_DAWN "dawn/status"

/* Timing constants */
#define STATUS_HEARTBEAT_INTERVAL_SEC 30
#define STATUS_TIMEOUT_SEC 90

/**
 * @brief Set Last Will and Testament before connecting
 *
 * Must be called BEFORE mosquitto_connect() to set up
 * automatic offline notification on disconnect.
 *
 * @param mosq Mosquitto instance
 * @return 0 on success, 1 on failure
 */
int component_status_set_lwt(struct mosquitto *mosq);

/**
 * @brief Initialize component status system
 *
 * Call AFTER mosquitto connects (in on_connect callback).
 * - Subscribes to dawn/status
 * - Publishes online status
 * - Starts heartbeat thread
 *
 * @param mosq Mosquitto instance
 * @return 0 on success, 1 on failure
 */
int component_status_init(struct mosquitto *mosq);

/**
 * @brief Publish offline status before disconnect
 *
 * Call BEFORE mosquitto_disconnect() for graceful shutdown.
 *
 * @param mosq Mosquitto instance
 */
void component_status_publish_offline(struct mosquitto *mosq);

/**
 * @brief Shutdown component status system
 *
 * Stops heartbeat thread and cleans up resources.
 */
void component_status_shutdown(void);

/**
 * @brief Handle incoming status messages
 *
 * Routes dawn/status messages to update Dawn online state.
 *
 * @param topic MQTT topic
 * @param payload Message payload
 * @param payloadlen Payload length
 */
void component_status_handle_message(const char *topic, const char *payload, int payloadlen);

/**
 * @brief Check if Dawn is currently online
 *
 * @return true if Dawn is online and not timed out
 */
bool component_status_is_dawn_online(void);

/**
 * @brief Get seconds since last Dawn heartbeat
 *
 * @return Seconds since last heartbeat, or -1 if never received
 */
int component_status_get_dawn_age(void);

#endif /* COMPONENT_STATUS_H */
