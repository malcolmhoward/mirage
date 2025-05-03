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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <stdbool.h>
#include <mosquitto.h>

#include "screenshot.h"
#include "config_manager.h"
#include "logging.h"
#include "image_utils.h"
#include "mirage.h"
#include "recording.h"

/* Global variables for PBO system */
static GLuint g_pboIds[2] = {0, 0};
static int g_pboIndex = 0;
static bool g_pboInitialized = false;

/* Added tracking for last successful mapping */
static int g_lastSuccessfulPboIndex = -1;
static bool g_hasValidLastFrame = false;

/* Screenshot request handling */
static pthread_mutex_t g_screenshot_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_screenshot_requested = 0;
static char g_screenshot_path[PATH_MAX+29] = "";
static int g_screenshot_with_overlay = 0;
static screenshot_t g_screenshot_source = SCREENSHOT_MANUAL;

/* Recording path where screenshots are saved */
static char record_path[PATH_MAX] = ".";

/* Set the recording path for screenshots */
void set_screenshot_recording_path(const char *path) {
   if (path != NULL) {
      strncpy(record_path, path, PATH_MAX - 1);
      record_path[PATH_MAX - 1] = '\0';
   }
}

/**
 * Initializes the Pixel Buffer Object (PBO) system used for asynchronous 
 * frame capture from OpenGL.
 */
void init_pbo_system(void) {
    if (g_pboInitialized) {
        glDeleteBuffers(2, g_pboIds);
    }

    glGenBuffers(2, g_pboIds);
    g_pboInitialized = true;
    g_lastSuccessfulPboIndex = -1;
    g_hasValidLastFrame = false;

    LOG_INFO("PBO system initialized with buffer tracking.");
}

/**
 * Cleans up resources allocated by the PBO system.
 */
void cleanup_pbo_system(void) {
    if (g_pboInitialized) {
        glDeleteBuffers(2, g_pboIds);
        g_pboInitialized = false;
    }
    g_lastSuccessfulPboIndex = -1;
    g_hasValidLastFrame = false;
}

/**
 * Requests a screenshot to be taken by the main thread.
 * This function is thread-safe and can be called from any thread.
 *
 * @param with_overlay Whether to include UI overlay
 * @param full_resolution Whether to maintain full resolution
 * @param output_filename Path to save the screenshot (or NULL for auto-generated)
 * @param source The source of the screenshot request
 * @return 0 if request was queued successfully, non-zero otherwise
 */
int request_screenshot(int with_overlay, int full_resolution,
                       const char *output_filename, screenshot_t source) {
    int result = SUCCESS;

    pthread_mutex_lock(&g_screenshot_mutex);

    g_screenshot_source = source;

    /* If there's already a pending request, don't overwrite it */
    if (g_screenshot_requested) {
        LOG_WARNING("Screenshot already requested, ignoring new request");
        result = FAILURE;
    } else {
        /* Set the request flag */
        g_screenshot_requested = 1;
        g_screenshot_with_overlay = with_overlay;

        /* Store the full resolution flag as bit 1 in the request flag */
        if (full_resolution) {
            g_screenshot_requested |= 2;
        }

        /* Store the filename if provided */
        if (output_filename != NULL) {
            strncpy(g_screenshot_path, output_filename, sizeof(g_screenshot_path) - 1);
            g_screenshot_path[sizeof(g_screenshot_path) - 1] = '\0';
        } else {
            g_screenshot_path[0] = '\0';  /* Empty string indicates auto-generated filename */
        }

        LOG_INFO("Screenshot requested: overlay=%d, full_res=%d, path=%s",
                with_overlay, full_resolution,
                output_filename ? output_filename : "auto-generated");
    }

    pthread_mutex_unlock(&g_screenshot_mutex);

    return result;
}

/**
 * Asynchronously reads pixels from the current OpenGL framebuffer into a user buffer.
 * Uses a double-buffered approach with PBOs for improved performance.
 *
 * @param renderer  Pointer to the SDL_Renderer (must be using an OpenGL backend).
 * @param rect      Optional rectangle specifying the area to read.
 * @param format    SDL pixel format.
 * @param pixels    Pointer to the user-allocated buffer for storing the pixels.
 * @param pitch     Byte pitch (row stride) of the user buffer.
 *
 * @return 0 on success, 1 on failure.
 */
int OpenGL_RenderReadPixelsAsync(SDL_Renderer *renderer,
                                const SDL_Rect *rect,
                                Uint32 format,
                                void *pixels,
                                int pitch)
{
   /* Basic parameter checks */
   if (!renderer || !pixels) {
      LOG_ERROR("Invalid arguments: renderer=%p, pixels=%p",
                (void*)renderer, (void*)pixels);
      return 1;
   }

   /* Grab current GL context from SDL (assumes we've made it current) */
   SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
   if (!currentContext) {
      LOG_ERROR("No current GL context found.");
      return 1;
   }

   /* Determine the rectangle to read */
   int readX = 0, readY = 0, readW = 0, readH = 0;
   if (rect) {
      readX = rect->x;
      readY = rect->y;
      readW = rect->w;
      readH = rect->h;
   } else {
      /* If rect is NULL, read the entire render target */
      SDL_GetRendererOutputSize(renderer, &readW, &readH);
   }

   /* Lazy-init the PBOs if needed */
   if (!g_pboInitialized) {
      init_pbo_system();
   }

   /* Calculate the data size (assuming RGBA 8-bit) */
   const int bytesPerPixel = 4;  /* RGBA */
   const GLsizeiptr dataSize = (GLsizeiptr)(readW * readH * bytesPerPixel);

   /* Bind the "current" PBO for asynchronous readback */
   glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[g_pboIndex]);
   glBufferData(GL_PIXEL_PACK_BUFFER, dataSize, NULL, GL_STREAM_READ);

   /* Kick off the async read from the current framebuffer */
   glReadPixels(readX, readY, readW, readH, GL_RGBA, GL_UNSIGNED_BYTE, 0);

   /* Now bind the "previous" PBO and try to map it to system memory */
   int prevIndex = (g_pboIndex + 1) % 2;
   glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[prevIndex]);

   /* Add retry logic for buffer mapping */
   GLubyte* mappedBuffer = NULL;
   int retryCount = 0;
   const int MAX_RETRIES = 3;
   bool usedFallbackBuffer = false;

   while (retryCount < MAX_RETRIES) {
      /* Try to map the buffer */
      mappedBuffer = (GLubyte*)glMapBufferRange(GL_PIXEL_PACK_BUFFER,
                                                0,
                                                dataSize,
                                                GL_MAP_READ_BIT);

      if (mappedBuffer) {
         /* Success! Break out of the retry loop */
         break;
      } else {
         /* Mapping failed - log and retry after a brief delay */
         if (retryCount < MAX_RETRIES - 1) {
            LOG_WARNING("Buffer mapping failed, retrying (%d/%d)...",
                        retryCount + 1, MAX_RETRIES);

            /* Introduce a small delay to allow GPU to finish */
            SDL_Delay(5);  /* 5ms delay should be brief enough not to impact interactivity */
         } else {
            LOG_WARNING("Buffer mapping failed after %d retries.", MAX_RETRIES);
         }
         retryCount++;
      }
   }

   /* If mapping still failed after retries, check if we have a last known good buffer */
   if (!mappedBuffer) {
      if (g_hasValidLastFrame && g_lastSuccessfulPboIndex >= 0) {
         /* We have a previously mapped buffer - switch to it */
         glUnmapBuffer(GL_PIXEL_PACK_BUFFER);  /* Unmap current buffer */
         glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[g_lastSuccessfulPboIndex]);

         /* Try to map the last known good buffer */
         mappedBuffer = (GLubyte*)glMapBufferRange(GL_PIXEL_PACK_BUFFER,
                                                  0,
                                                  dataSize,
                                                  GL_MAP_READ_BIT);

         if (mappedBuffer) {
            LOG_INFO("Using last successful buffer from index %d", g_lastSuccessfulPboIndex);
            usedFallbackBuffer = true;
         } else {
            LOG_WARNING("Failed to map last known good buffer. Screenshot may be incomplete.");
         }
      } else {
         /* No usable previously mapped buffer */
         LOG_WARNING("No previous good buffer available. Screenshot may be incomplete.");
      }
   }

   if (mappedBuffer) {
      /* Copy the pixel data into the user-provided buffer */
      for (int y = 0; y < readH; ++y) {
         int flippedY = (readH - 1) - y; /* Invert the row index */
         GLubyte* dstRow = (GLubyte*)pixels + (flippedY * pitch);
         GLubyte* srcRow = mappedBuffer + (y * readW * bytesPerPixel);
         memcpy(dstRow, srcRow, readW * bytesPerPixel);
      }

      /* Unmap buffer */
      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

      /* If this was a successful current frame mapping (not a fallback),
         update our tracking of last successful buffer */
      if (!usedFallbackBuffer) {
         g_lastSuccessfulPboIndex = prevIndex;
         g_hasValidLastFrame = true;
      }
   }

   /* Unbind the PBO */
   glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

   /* Flip the index so next call reads the other buffer */
   g_pboIndex = prevIndex;

   /* Return success if we got data */
   return (mappedBuffer != NULL) ? 0 : 1;
}

/**
 * Takes a screenshot with specified options for overlay and resolution.
 */
int take_screenshot(int with_overlay, int no_camera_mode, int full_resolution, const char *output_filename) {
   hud_display_settings *this_hds = get_hud_display_settings();
   SDL_Renderer *renderer = get_sdl_renderer();
   video_out_data *this_vod = get_video_out_data();
   time_t r_time;
   struct tm *l_time = NULL;
   char datetime[16];
   char filename[PATH_MAX+31];
   int result = 0;

   /* Generate timestamp for the filename if needed */
   if (output_filename == NULL) {
      time(&r_time);
      l_time = localtime(&r_time);
      strftime(datetime, sizeof(datetime), "%Y%m%d_%H%M%S", l_time);

      /* Generate filename with timestamp */
      snprintf(filename, sizeof(filename), "%s/screenshot-%s.jpg", record_path, datetime);
   } else {
      strncpy(filename, output_filename, PATH_MAX+31-1);
      filename[PATH_MAX+31-1] = '\0';
   }

   LOG_INFO("Taking screenshot: %s, overlay: %d, full res: %d",
            filename, with_overlay, full_resolution);

   if (with_overlay) {
      /* With overlay - capture what's currently on screen */
      void *screenshot_buffer =
          malloc(this_hds->eye_output_width * 2 * RGB_OUT_SIZE * this_hds->eye_output_height);
      if (screenshot_buffer == NULL) {
         LOG_ERROR("Unable to allocate memory for screenshot buffer");
         return FAILURE;
      }

      /* Use the PBO-based async read instead of synchronized SDL function */
      result = OpenGL_RenderReadPixelsAsync(
         renderer,
         NULL,
         PIXEL_FORMAT_OUT,
         screenshot_buffer,
         this_hds->eye_output_width * 2 * RGB_OUT_SIZE
      );
      if (result != 0) {
         LOG_ERROR("Failed to read pixels: %d", result);
         free(screenshot_buffer);
         return FAILURE;
      }

      /* Calculate dimensions based on resolution preference */
      int new_width, new_height;
      if (full_resolution) {
         new_width = this_hds->eye_output_width;
         new_height = this_hds->eye_output_height;
      } else {
         new_width = SNAPSHOT_WIDTH;
         new_height = SNAPSHOT_HEIGHT;
      }

      /* Process and save the image */
      ImageProcessParams params = {
         .rgba_buffer = (unsigned char *)screenshot_buffer,
         .orig_width = this_hds->eye_output_width * 2,
         .orig_height = this_hds->eye_output_height,
         .filename = filename,
         .left_crop = 0,
         .top_crop = 0,
         .right_crop = this_hds->eye_output_width, /* Capture just the left eye */
         .bottom_crop = 0,
         .new_width = new_width,
         .new_height = new_height,
         .format_params.quality = full_resolution ? 95 : SNAPSHOT_QUALITY /* Higher quality for full-res */
      };

      result = process_and_save_image(&params);
      free(screenshot_buffer);

   } else {
      /* Without overlay - capture raw camera feed */
      /* Use a mutex lock before accessing shared buffers */
      pthread_mutex_lock(&this_vod->p_mutex);

      void *snapshot_pixel = NULL;
      void *temp_buffer = NULL;

      /* If we're using RGB buffers from recording */
      if (this_vod->rgb_out_pixels[this_vod->buffer_num] != NULL) {
         snapshot_pixel = this_vod->rgb_out_pixels[this_vod->buffer_num];
      }
      /* If recording buffer is not available, get directly from camera frame buffer */
      else if (!no_camera_mode) {
         /* Use camera frame buffer if available */
         snapshot_pixel = grab_latest_camera_frame(temp_buffer);
      }

      if (snapshot_pixel == NULL) {
         pthread_mutex_unlock(&this_vod->p_mutex);
         LOG_ERROR("No valid pixel data available for screenshot");
         return FAILURE;
      }

      /* Calculate dimensions based on resolution preference */
      int new_width, new_height;
      if (full_resolution) {
         new_width = this_hds->cam_input_width - (2 * this_hds->cam_crop_x);
         new_height = this_hds->cam_input_height;
      } else {
         new_width = SNAPSHOT_WIDTH;
         new_height = SNAPSHOT_HEIGHT;
      }

      ImageProcessParams params = {
         .rgba_buffer = (unsigned char *)snapshot_pixel,
         .orig_width = this_hds->cam_input_width,
         .orig_height = this_hds->cam_input_height,
         .filename = filename,
         .left_crop = this_hds->cam_crop_x,
         .top_crop = 0,
         .right_crop = this_hds->cam_crop_x,
         .bottom_crop = 0,
         .new_width = new_width,
         .new_height = new_height,
         .format_params.quality = full_resolution ? 95 : SNAPSHOT_QUALITY /* Higher quality for full-res */
      };

      result = process_and_save_image(&params);

      /* Free the temporary buffer if we allocated one */
      if (temp_buffer != NULL) {
         free(temp_buffer);
      }

      pthread_mutex_unlock(&this_vod->p_mutex);
   }

   if (result != 0) {
      LOG_ERROR("Image processing failed with error code: %d", result);
      return 3;
   } else {
      LOG_INFO("Screenshot saved to: %s", filename);
      return 0;
   }
}

/**
 * Takes a snapshot for AI processing and saves it to disk.
 * This function queues the request to be processed by the main thread.
 *
 * @param datetime String containing the datetime for the snapshot filename (optional)
 *                 If NULL, a current timestamp will be generated
 */
void trigger_snapshot(const char *datetime) {
    hud_display_settings *this_hds = get_hud_display_settings();
    char snapshot_path[PATH_MAX+29];

    /* Generate fresh timestamp if none was provided */
    char fresh_datetime[16];
    if (datetime == NULL || datetime[0] == '\0') {
        time_t r_time;
        struct tm *l_time = NULL;

        time(&r_time);
        l_time = localtime(&r_time);
        strftime(fresh_datetime, sizeof(fresh_datetime), "%Y%m%d_%H%M%S", l_time);
        datetime = fresh_datetime;
    }

    /* Format the filename with the timestamp */
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/snapshot-%s.jpg",
            record_path, datetime);

    /* Queue the screenshot request based on configuration */
    request_screenshot(this_hds->snapshot_overlay, 0, snapshot_path, SCREENSHOT_MQTT);
}

/**
 * Notifies the "dawn" process that a "viewing" command has completed, providing a snapshot
 * filename for processing.
 *
 * @param filename The filename of the snapshot generated by the viewing command.
 */
void mqttViewingSnapshot(const char *filename) {
   char mqtt_command[1024] = "";

   /* Construct the MQTT command with the snapshot filename */
   snprintf(mqtt_command, sizeof(mqtt_command),
      "{ \"device\": \"viewing\", \"action\": \"completed\", \"value\": \"%s\" }",
      filename);
   LOG_INFO("Sending: %s", mqtt_command);

   mqttSendMessage("dawn", mqtt_command);
}

/**
 * Process any pending screenshot requests in the main thread.
 * Checks if a screenshot has been requested and if so, takes the screenshot with the
 * requested parameters and clears the request flag.
 */
void process_screenshot_requests(int no_camera_mode) {
    pthread_mutex_lock(&g_screenshot_mutex);

    if (g_screenshot_requested) {
        int with_overlay = g_screenshot_with_overlay;
        int full_resolution = (g_screenshot_requested & 2) ? 1 : 0;
        screenshot_t source = g_screenshot_source;
        char output_path[PATH_MAX+31];

        /* Copy the path to a local variable */
        if (g_screenshot_path[0] != '\0') {
            strncpy(output_path, g_screenshot_path, sizeof(output_path) - 1);
            output_path[sizeof(output_path) - 1] = '\0';

            /* For MQTT requests, ensure the timestamp is current by updating
               the filename if it starts with "snapshot-" */
            if (source == SCREENSHOT_MQTT && strstr(output_path, "/snapshot-") != NULL) {
                char *base_path = strdup(output_path);
                char *timestamp_part = strstr(base_path, "/snapshot-");
                if (timestamp_part) {
                    *timestamp_part = '\0'; /* Truncate at /snapshot- */

                    /* Generate a fresh timestamp */
                    time_t r_time;
                    struct tm *l_time = NULL;
                    char datetime[16];

                    time(&r_time);
                    l_time = localtime(&r_time);
                    strftime(datetime, sizeof(datetime), "%Y%m%d_%H%M%S", l_time);

                    /* Recreate filename with fresh timestamp */
                    snprintf(output_path, sizeof(output_path), "%s/snapshot-%s.jpg",
                            base_path, datetime);
                }
                free(base_path);
            }
        } else {
            /* Generate a filename with timestamp */
            time_t r_time;
            struct tm *l_time = NULL;
            char datetime[16];

            time(&r_time);
            l_time = localtime(&r_time);
            strftime(datetime, sizeof(datetime), "%Y%m%d_%H%M%S", l_time);

            snprintf(output_path, sizeof(output_path), "%s/screenshot-%s.jpg",
                    record_path, datetime);
        }

        /* Clear the request flag before taking the screenshot */
        g_screenshot_requested = 0;
        g_screenshot_path[0] = '\0';
        g_screenshot_source = SCREENSHOT_MANUAL;

        pthread_mutex_unlock(&g_screenshot_mutex);

        /* Now take the screenshot from the main thread where OpenGL context is valid */
        int result = take_screenshot(with_overlay, no_camera_mode, full_resolution, output_path);

        /* Send notification if it was an MQTT request */
        if (source == SCREENSHOT_MQTT && result == 0) {
           LOG_INFO("Screenshot for MQTT. Sending...");
           mqttViewingSnapshot(output_path);
        }
    } else {
        pthread_mutex_unlock(&g_screenshot_mutex);
    }
}
