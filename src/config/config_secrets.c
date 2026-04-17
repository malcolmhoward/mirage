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
 * Runtime secrets loader -- reads API keys from secrets.json.
 */

#include "config/config_secrets.h"

#include <json-c/json.h>
#include <string.h>

#include "logging.h"
#include "util/string_utils.h"

#define MAX_SECRET_LENGTH 256

static char youtube_stream_key[MAX_SECRET_LENGTH] = "";
static char google_api_key[MAX_SECRET_LENGTH] = "";
static char mqtt_username[MAX_SECRET_LENGTH] = "";
static char mqtt_password[MAX_SECRET_LENGTH] = "";
static char dawn_url[MAX_SECRET_LENGTH] = "";
static char dawn_service_token[MAX_SECRET_LENGTH] = "";

/**
 * @brief Read a string value from a JSON object into a buffer.
 */
static void read_secret(struct json_object *root, const char *key, char *dest, size_t dest_size) {
   struct json_object *val = NULL;
   if (json_object_object_get_ex(root, key, &val) && val != NULL) {
      safe_strncpy(dest, json_object_get_string(val), dest_size);
   }
}

int secrets_load(const char *filename) {
   struct json_object *root = json_object_from_file(filename);
   if (root == NULL) {
      OLOG_WARNING("Could not load secrets file: %s (continuing without secrets)", filename);
      return 1;
   }

   read_secret(root, "youtube_stream_key", youtube_stream_key, sizeof(youtube_stream_key));
   read_secret(root, "google_api_key", google_api_key, sizeof(google_api_key));
   read_secret(root, "mqtt_username", mqtt_username, sizeof(mqtt_username));
   read_secret(root, "mqtt_password", mqtt_password, sizeof(mqtt_password));
   read_secret(root, "dawn_url", dawn_url, sizeof(dawn_url));
   read_secret(root, "dawn_service_token", dawn_service_token, sizeof(dawn_service_token));

   json_object_put(root);
   OLOG_INFO("Secrets loaded from %s", filename);
   return 0;
}

const char *get_youtube_stream_key(void) {
   return youtube_stream_key;
}

const char *get_google_api_key(void) {
   return google_api_key;
}

const char *get_mqtt_username(void) {
   return mqtt_username;
}

const char *get_mqtt_password(void) {
   return mqtt_password;
}

const char *get_dawn_url(void) {
   return dawn_url;
}

const char *get_dawn_service_token(void) {
   return dawn_service_token;
}
