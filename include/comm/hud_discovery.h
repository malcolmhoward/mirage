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
 * HUD Discovery - MQTT-based capability advertisement
 *
 * This module publishes Mirage's HUD capabilities (elements and modes) via MQTT
 * so that DAWN can dynamically discover available options.
 */

#ifndef HUD_DISCOVERY_H
#define HUD_DISCOVERY_H

#include <mosquitto.h>

/* MQTT Discovery Topics */
#define HUD_DISCOVERY_TOPIC_ELEMENTS "hud/discovery/elements"
#define HUD_DISCOVERY_TOPIC_MODES "hud/discovery/modes"
#define HUD_DISCOVERY_TOPIC_REQUEST "hud/discovery/request"

/**
 * @brief Initialize the HUD discovery subsystem
 *
 * Subscribes to discovery request topic.
 *
 * @param mosq Mosquitto client instance
 * @return 0 on success, non-zero on error
 */
int hud_discovery_init(struct mosquitto *mosq);

/**
 * @brief Publish HUD discovery information
 *
 * Publishes current HUD elements and modes to discovery topics
 * with retain flag so new subscribers receive the information.
 * This should be called on connect and when requested.
 *
 * @param mosq Mosquitto client instance
 */
void hud_discovery_publish(struct mosquitto *mosq);

/**
 * @brief Handle incoming discovery request
 *
 * Called when a discovery request message is received.
 * Republishes the current discovery information.
 *
 * @param mosq Mosquitto client instance
 * @param payload Request message payload (JSON)
 */
void hud_discovery_handle_request(struct mosquitto *mosq, const char *payload);

#endif /* HUD_DISCOVERY_H */
