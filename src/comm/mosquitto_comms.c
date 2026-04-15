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
 */

#include <mosquitto.h>
#include <stdio.h>

#include "comm/command_processing.h"
#include "comm/component_status.h"
#include "comm/hud_discovery.h"
#include "config/config_manager.h"
#include "config/config_parser.h"
#include "config/defines.h"
#include "hardware/armor.h"
#include "util/logging.h"

/* Mosquitto STUFF */
/* Callback called when the client receives a CONNACK message from the broker. */
void on_connect(struct mosquitto *mosq, void *obj, int reason_code) {
   int rc;
   armor_settings *this_as = get_armor_settings();
   element *this_element = this_as->armor_elements;

   if (this_as->armor_elements == NULL) {
      return;
   }

   if (reason_code != 0) {
      mosquitto_disconnect(mosq);
      return;
   }

   LOG_INFO("Mosquitto successfully connected. Subscribing to...");

   // Subscribe to the main hud service
   rc = mosquitto_subscribe(mosq, NULL, "hud", 1);
   if (rc != MOSQ_ERR_SUCCESS) {
      LOG_ERROR("Error subscribing to hud: %s", mosquitto_strerror(rc));
   }

   // Subscribe to the helmet topic for faceplate control
   rc = mosquitto_subscribe(mosq, NULL, "helmet", 1);
   if (rc != MOSQ_ERR_SUCCESS) {
      LOG_ERROR("Error subscribing to helmet: %s", mosquitto_strerror(rc));
   }

   // Subscribe to the stat telemetry topic for system metrics
   rc = mosquitto_subscribe(mosq, NULL, "stat/telemetry", 1);
   if (rc != MOSQ_ERR_SUCCESS) {
      LOG_ERROR("Error subscribing to stat/telemetry: %s", mosquitto_strerror(rc));
   }

   // Subscribe to the stat status topic for component presence
   rc = mosquitto_subscribe(mosq, NULL, "stat/status", 1);
   if (rc != MOSQ_ERR_SUCCESS) {
      LOG_ERROR("Error subscribing to stat/status: %s", mosquitto_strerror(rc));
   }

   /* This works. I think I like the idea of a registration service better but... */
   while (this_element != NULL) {
      LOG_INFO(" %s\n", this_element->mqtt_device);
      rc = mosquitto_subscribe(mosq, NULL, this_element->mqtt_device, 1);
      if (rc != MOSQ_ERR_SUCCESS) {
         LOG_ERROR("Error subscribing: %s", mosquitto_strerror(rc));
      }

      this_element = this_element->next;
   }

   /* Initialize HUD discovery (subscribes to request topic) */
   if (hud_discovery_init(mosq) == 0) {
      /* Publish discovery information (retained) */
      hud_discovery_publish(mosq);
   }

   /* Initialize component status (subscribes to dawn/status, publishes online, starts heartbeat) */
   component_status_init(mosq);
}

/* Callback called when the broker sends a SUBACK in response to a SUBSCRIBE. */
void on_subscribe(struct mosquitto *mosq,
                  void *obj,
                  int mid,
                  int qos_count,
                  const int *granted_qos) {
   int i;
   bool have_subscription = false;

   for (i = 0; i < qos_count; i++) {
      if (granted_qos[i] <= 2) {
         have_subscription = true;
      }
   }
   if (have_subscription == false) {
      LOG_ERROR("Error: All subscriptions rejected.");
      mosquitto_disconnect(mosq);
   }
}

/* Callback called when the client receives a message. */
void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
   // Properly null-terminate the payload to pass around.
   char *payload = malloc(msg->payloadlen + 1);
   if (!payload) {
      LOG_ERROR("Failed to allocate memory for MQTT payload");
      return;
   }

   memcpy(payload, msg->payload, msg->payloadlen);
   payload[msg->payloadlen] = '\0';

   //LOG_INFO("%s %d %s", msg->topic, msg->qos, (char *)msg->payload);

   /* Handle HUD discovery requests */
   if (strcmp(msg->topic, HUD_DISCOVERY_TOPIC_REQUEST) == 0) {
      hud_discovery_handle_request(mosq, payload);
      free(payload);
      return;
   }

   /* Handle Dawn status messages (component keepalive) */
   if (strcmp(msg->topic, STATUS_TOPIC_DAWN) == 0) {
      component_status_handle_message(msg->topic, payload, msg->payloadlen);
      free(payload);
      return;
   }

   /* Handle stat/status messages (component keepalive from STAT) */
   if (strcmp(msg->topic, "stat/status") == 0) {
      /* Currently just logged; could track STAT presence like Dawn */
      free(payload);
      return;
   }

   // Check if this is a helmet command that needs to be forwarded to serial
   if (strcmp(msg->topic, "helmet") == 0) {
      // Forward the message to serial if connected
      LOG_INFO("Received 'helmet' message to forward.");
      forward_helmet_command_to_serial(payload);
   }

   if (strcmp(msg->topic, "hud") != 0) {
      /* FIXME: Right now if it's not for "hud," I'm assuming it's from an armor component.
       * I probably need a better way. ;-)
       */
      registerArmor(msg->topic);
   }

   parse_json_command(payload, (char *)msg->topic);

   free(payload);
}
/* End Mosquitto Stuff */
