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

#ifndef CURL_DOWNLOAD_H
#define CURL_DOWNLOAD_H

#include "defines.h"

/* Curl Image Download Data */
struct curl_data {
   char url[512];
   int update_interval_sec;
   int updated;         // Flag set when new data is available, cleared when consumed

   /* curl download area */
   size_t size;
   char *data;

   /* Thread synchronization */
   pthread_mutex_t mutex;  // Added mutex to protect struct access
};

/**
 * Thread function for downloading map images at regular intervals
 *
 * This function runs in a separate thread and periodically downloads
 * map images from the URL specified in the curl_data struct. It stores
 * the raw data and sets the 'updated' flag when new data is available.
 *
 * @param arg Pointer to a struct curl_data with URL and other parameters
 * @return NULL when thread exits
 */
void *image_download_thread(void *arg);

#endif // CURL_DOWNLOAD_H

