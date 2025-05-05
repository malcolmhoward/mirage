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
static GLuint g_pboIds[3] = {0, 0, 0};
static int g_pboIndex = 0;
static int g_readIndex = 2;
static int g_writeIndex = 1;
static int g_frameCount = 0; // Track number of frames processed
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
      cleanup_pbo_system();
   }

   glGenBuffers(3, g_pboIds);

   int window_width = 0, window_height = 0;

   /* Get window dimensions */
   get_window_size(&window_width, &window_height);

   // Prime both buffers with initial data
   const GLsizeiptr dataSize = (GLsizeiptr)(window_width * window_height * 4);

   for (int i = 0; i < 3; i++) {
      glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[i]);
      glBufferData(GL_PIXEL_PACK_BUFFER, dataSize, NULL, GL_STREAM_READ);
   }

   // Start the initial frame to prime the first buffer
   glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[0]);
   glReadPixels(0, 0, window_width, window_height, GL_RGBA, GL_UNSIGNED_BYTE, 0);

   // Set the buffer to an unbounded state
   glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

   // Reset indices
   g_pboIndex = 0;
   g_readIndex = 2;
   g_writeIndex = 1;
   g_frameCount = 0;

   g_pboInitialized = true;
   g_lastSuccessfulPboIndex = -1;
   g_hasValidLastFrame = false;
}

/**
 * Cleans up resources allocated by the PBO system.
 */
void cleanup_pbo_system(void) {
    if (g_pboInitialized) {
        glDeleteBuffers(3, g_pboIds);
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
   glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[g_writeIndex]);
   glBufferData(GL_PIXEL_PACK_BUFFER, dataSize, NULL, GL_STREAM_READ);

   /* Kick off the async read from the current framebuffer */
   glReadPixels(readX, readY, readW, readH, GL_RGBA, GL_UNSIGNED_BYTE, 0);

   GLubyte* mappedBuffer = NULL;

   /* Skip trying to read during the first two frames -
      we need to prime the pipeline first */
   if (g_frameCount >= 2) {
      /* Now try to read from the read buffer */
      glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[g_readIndex]);

      mappedBuffer = (GLubyte*)glMapBufferRange(GL_PIXEL_PACK_BUFFER,
                                               0,
                                               dataSize,
                                               GL_MAP_READ_BIT);

      if (mappedBuffer) {
         /* Copy data with Y-flip */
         for (int y = 0; y < readH; ++y) {
            int flippedY = (readH - 1) - y;
            GLubyte* dstRow = (GLubyte*)pixels + (flippedY * pitch);
            GLubyte* srcRow = mappedBuffer + (y * readW * bytesPerPixel);
            memcpy(dstRow, srcRow, readW * bytesPerPixel);
         }

         glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
         g_lastSuccessfulPboIndex = g_readIndex;
         g_hasValidLastFrame = true;
      } else if (g_hasValidLastFrame) {
         /* Fall back to the last successful buffer if available */
         glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[g_lastSuccessfulPboIndex]);

         mappedBuffer = (GLubyte*)glMapBufferRange(GL_PIXEL_PACK_BUFFER,
                                                  0,
                                                  dataSize,
                                                  GL_MAP_READ_BIT);

         if (mappedBuffer) {
            /* Copy with Y-flip */
            for (int y = 0; y < readH; ++y) {
               int flippedY = (readH - 1) - y;
               GLubyte* dstRow = (GLubyte*)pixels + (flippedY * pitch);
               GLubyte* srcRow = mappedBuffer + (y * readW * bytesPerPixel);
               memcpy(dstRow, srcRow, readW * bytesPerPixel);
            }

            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
         }
      }
   } else {
      /* For the first 2 frames, we'll just return success without
         actually trying to read data, since we're still priming the pipeline */
      LOG_INFO("Priming PBO pipeline, frame %d", g_frameCount);
   }

   /* Unbind PBO */
   glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

   /* Cycle indices in a lockstep pattern */
   int temp = g_pboIndex;
   g_pboIndex = g_readIndex;
   g_readIndex = g_writeIndex;
   g_writeIndex = temp;

   /* Increment frame counter */
   g_frameCount++;

   /* For the first 2 frames, pretend we succeeded even though we didn't map anything */
   if (g_frameCount <= 2) {
      return 0;
   }

   return (mappedBuffer != NULL) ? 0 : 1;
}

/**
 * Synchronously reads pixels from the current OpenGL framebuffer into a user buffer.
 */
int OpenGL_RenderReadPixelsSync(SDL_Renderer *renderer,
                               const SDL_Rect *rect,
                               Uint32 format,
                               void *pixels,
                               int pitch) {
   /* Basic parameter checks */
   if (!renderer || !pixels) {
      LOG_ERROR("Invalid arguments for synchronous read");
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

   /* We'll need a temporary buffer to perform the Y-flip */
   const int bytesPerPixel = 4;  /* RGBA */
   GLubyte* tempBuffer = (GLubyte*)malloc(readW * readH * bytesPerPixel);
   if (!tempBuffer) {
      LOG_ERROR("Failed to allocate temporary buffer for pixel read");
      return 1;
   }

   /* Read pixels into our temporary buffer */
   glReadPixels(readX, readY, readW, readH, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer);

   /* Make sure the operation is complete */
   glFinish();

   /* Copy with Y-flipping to correct the orientation */
   for (int y = 0; y < readH; ++y) {
      int flippedY = (readH - 1) - y; /* Invert the row index */
      GLubyte* dstRow = (GLubyte*)pixels + (flippedY * pitch);
      GLubyte* srcRow = tempBuffer + (y * readW * bytesPerPixel);
      memcpy(dstRow, srcRow, readW * bytesPerPixel);
   }

   /* Clean up */
   free(tempBuffer);

   return 0;
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

      /* Use the PBO-based sync read */
      result = OpenGL_RenderReadPixelsSync(
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
      void *snapshot_pixel = NULL;
      void *temp_buffer = NULL;
      int orig_width, orig_height;
      int is_recording_buffer = 0;

      /* Use a mutex lock before accessing shared buffers */
      pthread_mutex_lock(&this_vod->p_mutex);

      if (!no_camera_mode) {
         /* Allocate buffer for camera frame - use the more efficient OpenGL method if possible */
         temp_buffer = malloc(this_hds->cam_input_width * this_hds->cam_input_height * 4);
         if (temp_buffer == NULL) {
            pthread_mutex_unlock(&this_vod->p_mutex);
            LOG_ERROR("Unable to allocate memory for camera frame buffer");
            return FAILURE;
         }

         /* Get frame data from camera */
         snapshot_pixel = grab_latest_camera_frame(temp_buffer);
         orig_width = this_hds->cam_input_width;
         orig_height = this_hds->cam_input_height;
      }

      if (snapshot_pixel == NULL) {
         if (temp_buffer != NULL) {
            free(temp_buffer);
         }
         pthread_mutex_unlock(&this_vod->p_mutex);
         LOG_ERROR("No valid pixel data available for screenshot");
         return FAILURE;
      }

      /* Calculate dimensions based on resolution preference */
      int new_width, new_height;
      if (full_resolution) {
         if (is_recording_buffer) {
            new_width = this_hds->eye_output_width;
            new_height = this_hds->eye_output_height;
         } else {
            new_width = this_hds->cam_input_width - (2 * this_hds->cam_crop_x);
            new_height = this_hds->cam_input_height;
         }
      } else {
         new_width = SNAPSHOT_WIDTH;
         new_height = SNAPSHOT_HEIGHT;
      }

      /* Adjust cropping based on buffer type */
      int left_crop, right_crop;
      if (is_recording_buffer) {
         /* For recording buffer (stereo), grab just the left eye */
         left_crop = 0;
         right_crop = this_hds->eye_output_width;
      } else {
         /* For camera buffer, use standard camera crop */
         left_crop = this_hds->cam_crop_x;
         right_crop = this_hds->cam_crop_x;
      }

      ImageProcessParams params = {
         .rgba_buffer = (unsigned char *)snapshot_pixel,
         .orig_width = orig_width,
         .orig_height = orig_height,
         .filename = filename,
         .left_crop = left_crop,
         .top_crop = 0,
         .right_crop = right_crop,
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
