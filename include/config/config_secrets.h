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
 * Runtime secrets loader -- reads API keys and credentials from secrets.json.
 * This file must never be committed with real values.
 */

#ifndef CONFIG_SECRETS_H
#define CONFIG_SECRETS_H

/**
 * @brief Load secrets from a JSON file.
 *
 * Reads the specified JSON file and populates internal secret storage.
 * Missing keys are left as empty strings. Call once at startup.
 *
 * @param filename Path to the secrets JSON file
 * @return 0 on success, non-zero on file read or parse error
 */
int secrets_load(const char *filename);

/** @brief Returns the YouTube stream key, or empty string if not set. */
const char *get_youtube_stream_key(void);

/** @brief Returns the Google API key, or empty string if not set. */
const char *get_google_api_key(void);

/** @brief Returns the MQTT username, or empty string if not set. */
const char *get_mqtt_username(void);

/** @brief Returns the MQTT password, or empty string if not set. */
const char *get_mqtt_password(void);

#endif /* CONFIG_SECRETS_H */
