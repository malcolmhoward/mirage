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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <curl/curl.h>

#include "SDL2/SDL.h"
#include "SDL2/SDL_image.h"

#include "curl_download.h"
#include "logging.h"
#include "mirage.h"

/**
 * Callback function for curl to write received data
 *
 * @param contents Pointer to received data
 * @param size Size of each data element
 * @param nmemb Number of data elements
 * @param userp User pointer (struct curl_data*)
 * @return Number of bytes processed
 */
static size_t write_data(void *contents, size_t size, size_t nmemb, void *userp) {
   size_t realsize = size * nmemb;
   struct curl_data *mem = (struct curl_data *)userp;
   char *ptr;

   /* No need for mutex here as this is called by curl_easy_perform
    * which is already protected by mutex in the calling function */

   ptr = realloc(mem->data, mem->size + realsize + 1);
   if (!ptr) {
      LOG_ERROR("Not enough memory for download (realloc returned NULL)");
      return 0;
   }

   mem->data = ptr;
   memcpy(&(mem->data[mem->size]), contents, realsize);
   mem->size += realsize;
   mem->data[mem->size] = 0;

   return realsize;
}

/**
 * Thread function for downloading map images at regular intervals
 */
void *image_download_thread(void *arg) {
   struct curl_data *this_data = (struct curl_data *)arg;
   CURL *curl_handle;
   CURLcode res;
   time_t last_update = 0;
   time_t current_time;
   int downloads = 0;

   /* Initialize curl */
   curl_handle = curl_easy_init();
   if (!curl_handle) {
      LOG_ERROR("Failed to initialize curl handle");
      return NULL;
   }

   while (!checkShutdown()) {
      time(&current_time);

      int should_refresh = 0;

      /* Check if it's time for an update or if a refresh is forced */
      pthread_mutex_lock(&this_data->mutex);
      should_refresh = (difftime(current_time, last_update) >= this_data->update_interval_sec) ||
                       this_data->force_refresh;

      if (this_data->force_refresh) {
         this_data->force_refresh = 0;  // Reset the flag
      }
      pthread_mutex_unlock(&this_data->mutex);

      /* Check if it's time for an update */
      if (should_refresh) {
         LOG_INFO("image_download_thread should_refresh received.");

         /* Lock the mutex before modifying shared data */
         pthread_mutex_lock(&this_data->mutex);

         /* Reset variables for new download */
         this_data->size = 0;
         if (this_data->data) {
            free(this_data->data);
            this_data->data = NULL;
         }

         pthread_mutex_unlock(&this_data->mutex);

         if ((this_data->download_count > 0) && (this_data->download_count - downloads <= 0)) {
            /* We've downloaded all we need to. This thread is being monitored though, so just sleep for a
             * while and then continue.
             */
            LOG_INFO("Download limit reached.");
            sleep(60);
            continue;
         }

         /* Set up curl options */
         curl_easy_setopt(curl_handle, CURLOPT_URL, this_data->url);
         curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);
         curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, this_data);
         curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);
         curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);

         /* Perform the request */
         res = curl_easy_perform(curl_handle);

         /* Lock mutex again for post-download updates */
         pthread_mutex_lock(&this_data->mutex);
         if (res == CURLE_OK && this_data->data && this_data->size > 0) {
            this_data->updated = 1;
            time(&last_update);
            LOG_INFO("Downloaded new map data, %zu bytes", this_data->size);
            downloads++;
         }
         pthread_mutex_unlock(&this_data->mutex);
      }

      /* Sleep a bit to avoid CPU spin */
      sleep(1);
   }

   /* Clean up */
   curl_easy_cleanup(curl_handle);

   /* Clean up shared resources one last time */
   pthread_mutex_lock(&this_data->mutex);

   if (this_data->data) {
      free(this_data->data);
      this_data->data = NULL;
   }
   this_data->size = 0;
   pthread_mutex_unlock(&this_data->mutex);

   return NULL;
}

