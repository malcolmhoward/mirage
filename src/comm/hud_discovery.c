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
 * HUD Discovery Implementation
 *
 * Publishes Mirage's HUD capabilities via MQTT for DAWN discovery.
 */

#include "comm/hud_discovery.h"

#include <json-c/json.h>
#include <string.h>
#include <time.h>

#include "config/config_manager.h"
#include "config/config_parser.h"
#include "core/mirage.h"
#include "ui/hud_manager.h"
#include "util/logging.h"

/**
 * @brief Check if a string is already in the JSON array
 */
static int json_array_contains_string(struct json_object *array, const char *str) {
   size_t len = json_object_array_length(array);
   for (size_t i = 0; i < len; i++) {
      struct json_object *item = json_object_array_get_idx(array, i);
      if (item && strcmp(json_object_get_string(item), str) == 0) {
         return 1;
      }
   }
   return 0;
}

/**
 * @brief Build JSON array of unique special element names
 *
 * Iterates through the main element list (get_first_element()) and collects
 * unique names of SPECIAL type elements. These are the controllable HUD element
 * types like armor_display, detect, map, info.
 *
 * Note: SPECIAL elements are in the main element list, not armor_elements.
 * armor_elements only contains ARMOR_COMPONENT type elements.
 *
 * Duplicates are filtered because the LLM only needs to know element types,
 * not every instance (e.g., there may be many gauge elements but the LLM
 * just needs "gauge" once).
 *
 * @return JSON array object (caller must free with json_object_put)
 */
static struct json_object *build_elements_array(void) {
   struct json_object *elements = json_object_new_array();

   element *el = get_first_element();
   if (el == NULL) {
      return elements;
   }

   while (el != NULL) {
      /* Only include SPECIAL type elements that have a special_name */
      if (el->type == SPECIAL && el->special_name[0] != '\0') {
         /* Skip duplicates */
         if (!json_array_contains_string(elements, el->special_name)) {
            json_object_array_add(elements, json_object_new_string(el->special_name));
         }
      }
      el = el->next;
   }

   return elements;
}

/**
 * @brief Build JSON array of HUD mode names
 *
 * Iterates through registered HUD screens and collects their names.
 *
 * @return JSON array object (caller must free with json_object_put)
 */
static struct json_object *build_modes_array(void) {
   struct json_object *modes = json_object_new_array();
   hud_manager *mgr = get_hud_manager();

   if (mgr == NULL || mgr->screens == NULL) {
      return modes;
   }

   hud_screen *screen = mgr->screens;
   while (screen != NULL) {
      if (screen->name[0] != '\0') {
         json_object_array_add(modes, json_object_new_string(screen->name));
      }
      screen = screen->next;
   }

   return modes;
}

static bool s_discovery_initialized = false;

int hud_discovery_init(struct mosquitto *mosq) {
   if (mosq == NULL) {
      return 1;
   }

   if (s_discovery_initialized) {
      return 0;
   }

   /* Subscribe to discovery request topic */
   int rc = mosquitto_subscribe(mosq, NULL, HUD_DISCOVERY_TOPIC_REQUEST, 0);
   if (rc != MOSQ_ERR_SUCCESS) {
      LOG_ERROR("HUD discovery: Failed to subscribe to %s: %s", HUD_DISCOVERY_TOPIC_REQUEST,
                mosquitto_strerror(rc));
      return 1;
   }

   s_discovery_initialized = true;
   LOG_INFO("HUD discovery: Subscribed to %s", HUD_DISCOVERY_TOPIC_REQUEST);
   return 0;
}

void hud_discovery_publish(struct mosquitto *mosq) {
   if (mosq == NULL) {
      return;
   }

   int rc;
   time_t now = time(NULL);

   /* Build and publish elements discovery message */
   {
      struct json_object *msg = json_object_new_object();
      json_object_object_add(msg, "device", json_object_new_string("mirage"));
      json_object_object_add(msg, "msg_type", json_object_new_string("discovery"));
      json_object_object_add(msg, "timestamp", json_object_new_int64((int64_t)now));
      json_object_object_add(msg, "elements", build_elements_array());

      const char *payload = json_object_to_json_string(msg);
      rc = mosquitto_publish(mosq, NULL, HUD_DISCOVERY_TOPIC_ELEMENTS, (int)strlen(payload),
                             payload, 0, true); /* retain=true */

      if (rc != MOSQ_ERR_SUCCESS) {
         LOG_ERROR("HUD discovery: Failed to publish elements: %s", mosquitto_strerror(rc));
      } else {
         LOG_INFO("HUD discovery: Published elements to %s", HUD_DISCOVERY_TOPIC_ELEMENTS);
      }

      json_object_put(msg);
   }

   /* Build and publish modes discovery message */
   {
      struct json_object *msg = json_object_new_object();
      json_object_object_add(msg, "device", json_object_new_string("mirage"));
      json_object_object_add(msg, "msg_type", json_object_new_string("discovery"));
      json_object_object_add(msg, "timestamp", json_object_new_int64((int64_t)now));
      json_object_object_add(msg, "huds", build_modes_array());

      const char *payload = json_object_to_json_string(msg);
      rc = mosquitto_publish(mosq, NULL, HUD_DISCOVERY_TOPIC_MODES, (int)strlen(payload), payload,
                             0, true); /* retain=true */

      if (rc != MOSQ_ERR_SUCCESS) {
         LOG_ERROR("HUD discovery: Failed to publish modes: %s", mosquitto_strerror(rc));
      } else {
         LOG_INFO("HUD discovery: Published modes to %s", HUD_DISCOVERY_TOPIC_MODES);
      }

      json_object_put(msg);
   }
}

void hud_discovery_handle_request(struct mosquitto *mosq, const char *payload) {
   if (mosq == NULL) {
      return;
   }

   /* Validate request is from a legitimate source (optional) */
   if (payload != NULL && payload[0] != '\0') {
      struct json_object *req = json_tokener_parse(payload);
      if (req != NULL) {
         struct json_object *msg_type_obj = NULL;
         if (json_object_object_get_ex(req, "msg_type", &msg_type_obj)) {
            const char *msg_type = json_object_get_string(msg_type_obj);
            if (msg_type == NULL || strcmp(msg_type, "discovery_request") != 0) {
               /* Not a discovery request, ignore */
               json_object_put(req);
               return;
            }
         }
         json_object_put(req);
      }
   }

   LOG_INFO("HUD discovery: Received discovery request, republishing capabilities");
   hud_discovery_publish(mosq);
}
