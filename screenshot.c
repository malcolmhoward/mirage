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

#include "screenshot.h"

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <json-c/json.h>
#include <mosquitto.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config_manager.h"
#include "image_utils.h"
#include "logging.h"
#include "mirage.h"
#include "recording.h"

/* Global variables for PBO system */
static GLuint g_pboIds[3] = { 0, 0, 0 };
static int g_pboIndex = 0;
static int g_readIndex = 2;
static int g_writeIndex = 1;
static int g_frameCount = 0;  // Track number of frames processed
static bool g_pboInitialized = false;

/* Added tracking for last successful mapping */
static int g_lastSuccessfulPboIndex = -1;
static bool g_hasValidLastFrame = false;

/* Screenshot request handling */
static pthread_mutex_t g_screenshot_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_screenshot_requested = 0;
static char g_screenshot_path[PATH_MAX + 29] = "";
static int g_screenshot_with_overlay = 0;
static screenshot_t g_screenshot_source = SCREENSHOT_MANUAL;
static char g_screenshot_request_id[128] = ""; /* OCP request_id for response correlation */

/* Recording path where screenshots are saved */
static char record_path[PATH_MAX] = ".";

/* Set the recording path for screenshots */
void set_screenshot_recording_path(const char *path) {
   if (path != NULL) {
      strncpy(record_path, path, PATH_MAX - 1);
      record_path[PATH_MAX - 1] = '\0';
   }
}

/* Base64 encoding table */
static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * Encode binary data to base64 string.
 * @param data Input binary data
 * @param input_length Length of input data
 * @return Heap-allocated base64 string (caller must free), or NULL on error
 */
static char *base64_encode(const unsigned char *data, size_t input_length) {
   size_t output_length = 4 * ((input_length + 2) / 3);
   char *encoded = malloc(output_length + 1);
   if (!encoded) {
      return NULL;
   }

   size_t i, j;
   for (i = 0, j = 0; i < input_length;) {
      uint32_t octet_a = i < input_length ? data[i++] : 0;
      uint32_t octet_b = i < input_length ? data[i++] : 0;
      uint32_t octet_c = i < input_length ? data[i++] : 0;
      uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

      encoded[j++] = base64_table[(triple >> 18) & 0x3F];
      encoded[j++] = base64_table[(triple >> 12) & 0x3F];
      encoded[j++] = base64_table[(triple >> 6) & 0x3F];
      encoded[j++] = base64_table[triple & 0x3F];
   }

   /* Add padding */
   int mod = input_length % 3;
   if (mod > 0) {
      encoded[output_length - 1] = '=';
      if (mod == 1) {
         encoded[output_length - 2] = '=';
      }
   }

   encoded[output_length] = '\0';
   return encoded;
}

/**
 * Read file contents into memory.
 * @param filename Path to file
 * @param size_out Output: file size
 * @return Heap-allocated buffer (caller must free), or NULL on error
 */
static unsigned char *read_file_contents(const char *filename, size_t *size_out) {
   FILE *fp = fopen(filename, "rb");
   if (!fp) {
      return NULL;
   }

   fseek(fp, 0, SEEK_END);
   long size = ftell(fp);
   fseek(fp, 0, SEEK_SET);

   if (size <= 0) {
      fclose(fp);
      return NULL;
   }

   unsigned char *buffer = malloc(size);
   if (!buffer) {
      fclose(fp);
      return NULL;
   }

   size_t read = fread(buffer, 1, size, fp);
   fclose(fp);

   if (read != (size_t)size) {
      free(buffer);
      return NULL;
   }

   *size_out = (size_t)size;
   return buffer;
}

/* =============================================================================
 * OCP v1.1 Helpers (timestamp, checksum)
 * ============================================================================= */

/* Hex nibble lookup table for efficient conversion */
static const char hex_table[] = "0123456789abcdef";

/**
 * Get current Unix timestamp in milliseconds.
 * Uses clock_gettime(CLOCK_REALTIME) for consistency with DAWN.
 * @return Current time as milliseconds since epoch
 */
static int64_t get_timestamp_ms(void) {
   struct timespec ts;
   if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
      return 0;
   }
   return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/**
 * Compute SHA256 hash of data and return as lowercase hex string.
 * Uses EVP API (not deprecated in OpenSSL 3.0).
 * @param data Data to hash
 * @param len Length of data
 * @return Heap-allocated 65-byte hex string (caller must free), or NULL on error
 */
static char *sha256_compute(const unsigned char *data, size_t len) {
   if (!data || len == 0) {
      return NULL;
   }

   EVP_MD_CTX *ctx = EVP_MD_CTX_new();
   if (!ctx) {
      LOG_ERROR("sha256_compute: Failed to create EVP context");
      return NULL;
   }

   unsigned char hash[EVP_MAX_MD_SIZE];
   unsigned int hash_len = 0;

   if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 || EVP_DigestUpdate(ctx, data, len) != 1 ||
       EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
      LOG_ERROR("sha256_compute: SHA256 computation failed");
      EVP_MD_CTX_free(ctx);
      return NULL;
   }

   EVP_MD_CTX_free(ctx);

   char *hex = malloc(hash_len * 2 + 1);
   if (!hex) {
      return NULL;
   }

   /* Convert using lookup table for efficiency */
   for (unsigned int i = 0; i < hash_len; i++) {
      hex[i * 2] = hex_table[(hash[i] >> 4) & 0x0F];
      hex[i * 2 + 1] = hex_table[hash[i] & 0x0F];
   }
   hex[hash_len * 2] = '\0';

   return hex;
}

/**
 * Compute SHA256 hash of a file.
 * Uses EVP API (not deprecated in OpenSSL 3.0).
 * @param filepath Path to file
 * @return Heap-allocated 65-byte hex string (caller must free), or NULL on error
 */
static char *sha256_file(const char *filepath) {
   if (!filepath) {
      return NULL;
   }

   FILE *f = fopen(filepath, "rb");
   if (!f) {
      LOG_WARNING("sha256_file: Cannot open file: %s", filepath);
      return NULL;
   }

   EVP_MD_CTX *ctx = EVP_MD_CTX_new();
   if (!ctx) {
      LOG_ERROR("sha256_file: Failed to create EVP context");
      fclose(f);
      return NULL;
   }

   if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) {
      LOG_ERROR("sha256_file: Failed to initialize SHA256");
      EVP_MD_CTX_free(ctx);
      fclose(f);
      return NULL;
   }

   unsigned char buffer[8192];
   size_t bytes_read;
   while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
      if (EVP_DigestUpdate(ctx, buffer, bytes_read) != 1) {
         LOG_ERROR("sha256_file: Failed to update hash");
         EVP_MD_CTX_free(ctx);
         fclose(f);
         return NULL;
      }
   }
   fclose(f);

   unsigned char hash[EVP_MAX_MD_SIZE];
   unsigned int hash_len = 0;

   if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
      LOG_ERROR("sha256_file: Failed to finalize hash");
      EVP_MD_CTX_free(ctx);
      return NULL;
   }

   EVP_MD_CTX_free(ctx);

   char *hex = malloc(hash_len * 2 + 1);
   if (!hex) {
      return NULL;
   }

   /* Convert using lookup table for efficiency */
   for (unsigned int i = 0; i < hash_len; i++) {
      hex[i * 2] = hex_table[(hash[i] >> 4) & 0x0F];
      hex[i * 2 + 1] = hex_table[hash[i] & 0x0F];
   }
   hex[hash_len * 2] = '\0';

   return hex;
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
 */
int request_screenshot(int with_overlay,
                       int full_resolution,
                       const char *output_filename,
                       screenshot_t source) {
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
         g_screenshot_path[0] = '\0'; /* Empty string indicates auto-generated filename */
      }

      LOG_INFO("Screenshot requested: overlay=%d, full_res=%d, path=%s", with_overlay,
               full_resolution, output_filename ? output_filename : "auto-generated");
   }

   pthread_mutex_unlock(&g_screenshot_mutex);

   return result;
}

/**
 * Asynchronously reads pixels from the current OpenGL framebuffer into a user buffer.
 */
int OpenGL_RenderReadPixelsAsync(SDL_Renderer *renderer,
                                 const SDL_Rect *rect,
                                 Uint32 format,
                                 void *pixels,
                                 int pitch) {
   /* Basic parameter checks */
   if (!renderer || !pixels) {
      LOG_ERROR("Invalid arguments: renderer=%p, pixels=%p", (void *)renderer, (void *)pixels);
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
   const int bytesPerPixel = 4; /* RGBA */
   const GLsizeiptr dataSize = (GLsizeiptr)(readW * readH * bytesPerPixel);

   /* Bind the "current" PBO for asynchronous readback */
   glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[g_writeIndex]);
   glBufferData(GL_PIXEL_PACK_BUFFER, dataSize, NULL, GL_STREAM_READ);

   /* Kick off the async read from the current framebuffer */
   glReadPixels(readX, readY, readW, readH, GL_RGBA, GL_UNSIGNED_BYTE, 0);

   GLubyte *mappedBuffer = NULL;

   /* Skip trying to read during the first two frames -
      we need to prime the pipeline first */
   if (g_frameCount >= 2) {
      /* Now try to read from the read buffer */
      glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[g_readIndex]);

      mappedBuffer = (GLubyte *)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, dataSize,
                                                 GL_MAP_READ_BIT);

      if (mappedBuffer) {
         /* Copy data with Y-flip */
         for (int y = 0; y < readH; ++y) {
            int flippedY = (readH - 1) - y;
            GLubyte *dstRow = (GLubyte *)pixels + (flippedY * pitch);
            GLubyte *srcRow = mappedBuffer + (y * readW * bytesPerPixel);
            memcpy(dstRow, srcRow, readW * bytesPerPixel);
         }

         glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
         g_lastSuccessfulPboIndex = g_readIndex;
         g_hasValidLastFrame = true;
      } else if (g_hasValidLastFrame) {
         /* Fall back to the last successful buffer if available */
         glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[g_lastSuccessfulPboIndex]);

         mappedBuffer = (GLubyte *)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, dataSize,
                                                    GL_MAP_READ_BIT);

         if (mappedBuffer) {
            /* Copy with Y-flip */
            for (int y = 0; y < readH; ++y) {
               int flippedY = (readH - 1) - y;
               GLubyte *dstRow = (GLubyte *)pixels + (flippedY * pitch);
               GLubyte *srcRow = mappedBuffer + (y * readW * bytesPerPixel);
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
   const int bytesPerPixel = 4; /* RGBA */
   GLubyte *tempBuffer = (GLubyte *)malloc(readW * readH * bytesPerPixel);
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
      GLubyte *dstRow = (GLubyte *)pixels + (flippedY * pitch);
      GLubyte *srcRow = tempBuffer + (y * readW * bytesPerPixel);
      memcpy(dstRow, srcRow, readW * bytesPerPixel);
   }

   /* Clean up */
   free(tempBuffer);

   return 0;
}

/**
 * Takes a screenshot with specified options for overlay and resolution.
 */
int take_screenshot(int with_overlay,
                    int no_camera_mode,
                    int full_resolution,
                    const char *output_filename) {
   hud_display_settings *this_hds = get_hud_display_settings();
   SDL_Renderer *renderer = get_sdl_renderer();
   video_out_data *this_vod = get_video_out_data();
   time_t r_time;
   struct tm *l_time = NULL;
   char datetime[16];
   char filename[PATH_MAX + 31];
   int result = 0;

   /* Generate timestamp for the filename if needed */
   if (output_filename == NULL) {
      time(&r_time);
      l_time = localtime(&r_time);
      strftime(datetime, sizeof(datetime), "%Y%m%d_%H%M%S", l_time);

      /* Generate filename with timestamp */
      snprintf(filename, sizeof(filename), "%s/screenshot-%s.jpg", record_path, datetime);
   } else {
      strncpy(filename, output_filename, PATH_MAX + 31 - 1);
      filename[PATH_MAX + 31 - 1] = '\0';
   }

   LOG_INFO("Taking screenshot: %s, overlay: %d, full res: %d", filename, with_overlay,
            full_resolution);

   if (with_overlay) {
      /* With overlay - capture what's currently on screen */
      void *screenshot_buffer = malloc(this_hds->eye_output_width * 2 * RGB_OUT_SIZE *
                                       this_hds->eye_output_height);
      if (screenshot_buffer == NULL) {
         LOG_ERROR("Unable to allocate memory for screenshot buffer");
         return FAILURE;
      }

      /* Use the PBO-based sync read */
      result = OpenGL_RenderReadPixelsSync(renderer, NULL, PIXEL_FORMAT_OUT, screenshot_buffer,
                                           this_hds->eye_output_width * 2 * RGB_OUT_SIZE);
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
         .format_params.quality = full_resolution
                                      ? 95
                                      : SNAPSHOT_QUALITY /* Higher quality for full-res */
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
         .format_params.quality = full_resolution
                                      ? 95
                                      : SNAPSHOT_QUALITY /* Higher quality for full-res */
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
 */
void trigger_snapshot(const char *datetime, const char *request_id) {
   hud_display_settings *this_hds = get_hud_display_settings();
   char snapshot_path[PATH_MAX + 29];

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
   snprintf(snapshot_path, sizeof(snapshot_path), "%s/snapshot-%s.jpg", record_path, datetime);

   /* Store request_id for OCP response correlation */
   pthread_mutex_lock(&g_screenshot_mutex);
   if (request_id && request_id[0] != '\0') {
      strncpy(g_screenshot_request_id, request_id, sizeof(g_screenshot_request_id) - 1);
      g_screenshot_request_id[sizeof(g_screenshot_request_id) - 1] = '\0';
   } else {
      g_screenshot_request_id[0] = '\0';
   }
   pthread_mutex_unlock(&g_screenshot_mutex);

   /* Queue the screenshot request based on configuration */
   request_screenshot(this_hds->snapshot_overlay, 0, snapshot_path, SCREENSHOT_MQTT);
}

/**
 * Notifies the AI process that a "viewing" command has completed.
 * Follows OASIS Communications Protocol (OCP) format.
 *
 * @param filename The path to the snapshot file
 * @param request_id OCP request_id for correlation (can be NULL or empty)
 */
static void mqttViewingSnapshotOCP(const char *filename, const char *request_id) {
   struct json_object *response = json_object_new_object();
   if (!response) {
      LOG_ERROR("Failed to create JSON response object");
      return;
   }

   /* Required OCP fields */
   json_object_object_add(response, "device", json_object_new_string("viewing"));
   json_object_object_add(response, "action", json_object_new_string("completed"));
   json_object_object_add(response, "status", json_object_new_string("success"));

   /* Echo request_id if provided (OCP requirement) */
   if (request_id && request_id[0] != '\0') {
      json_object_object_add(response, "request_id", json_object_new_string(request_id));
   }

   /* OCP v1.1: Add timestamp */
   json_object_object_add(response, "timestamp", json_object_new_int64(get_timestamp_ms()));

   /* Check config for inline data mode */
   if (get_vision_inline_data()) {
      /* Read file and encode as base64 */
      size_t file_size = 0;
      unsigned char *file_data = read_file_contents(filename, &file_size);

      if (file_data) {
         /* OCP v1.1: Compute checksum of original data before encoding */
         char *checksum = sha256_compute(file_data, file_size);

         char *base64_data = base64_encode(file_data, file_size);
         free(file_data);

         if (base64_data) {
            /* Create data object per OCP spec */
            struct json_object *data_obj = json_object_new_object();
            json_object_object_add(data_obj, "type", json_object_new_string("image/jpeg"));
            json_object_object_add(data_obj, "encoding", json_object_new_string("base64"));
            json_object_object_add(data_obj, "content", json_object_new_string(base64_data));
            json_object_object_add(data_obj, "size", json_object_new_int64((int64_t)file_size));

            /* OCP v1.1: Add checksum to data object */
            if (checksum) {
               json_object_object_add(data_obj, "checksum", json_object_new_string(checksum));
            }

            json_object_object_add(response, "data", data_obj);

            /* Also include file path as fallback reference */
            json_object_object_add(response, "value", json_object_new_string(filename));

            free(base64_data);
            LOG_INFO("Sending inline image data: %zu bytes", file_size);
         } else {
            /* Base64 encoding failed, fall back to file path */
            json_object_object_add(response, "value", json_object_new_string(filename));
            LOG_WARNING("Base64 encoding failed, sending file path instead");
         }

         if (checksum) {
            free(checksum);
         }
      } else {
         /* File read failed, fall back to file path */
         json_object_object_add(response, "value", json_object_new_string(filename));
         LOG_WARNING("Failed to read file for inline data, sending path instead");
      }
   } else {
      /* File reference mode - just send the path */
      json_object_object_add(response, "value", json_object_new_string(filename));

      /* OCP v1.1: Add file checksum */
      char *checksum = sha256_file(filename);
      if (checksum) {
         json_object_object_add(response, "checksum", json_object_new_string(checksum));
         free(checksum);
      }

      LOG_INFO("Sending file reference: %s", filename);
   }

   const char *json_str = json_object_to_json_string(response);
   LOG_INFO("OCP Response: %.200s%s", json_str, strlen(json_str) > 200 ? "..." : "");

   mqttSendMessage("dawn", json_str);
   json_object_put(response);
}

/**
 * Sends an OCP-compliant error response for viewing command failures.
 *
 * @param request_id OCP request_id for correlation (can be NULL)
 * @param error_code Short error code (e.g., "CAPTURE_FAILED")
 * @param error_message Human-readable error description
 */
static void mqttViewingSnapshotErrorOCP(const char *request_id,
                                        const char *error_code,
                                        const char *error_message) {
   struct json_object *response = json_object_new_object();
   if (!response) {
      LOG_ERROR("Failed to create JSON error response object");
      return;
   }

   /* Required OCP fields */
   json_object_object_add(response, "device", json_object_new_string("viewing"));
   json_object_object_add(response, "action", json_object_new_string("completed"));
   json_object_object_add(response, "status", json_object_new_string("error"));

   /* Echo request_id if provided (OCP requirement) */
   if (request_id && request_id[0] != '\0') {
      json_object_object_add(response, "request_id", json_object_new_string(request_id));
   }

   /* OCP v1.1: Add timestamp */
   json_object_object_add(response, "timestamp", json_object_new_int64(get_timestamp_ms()));

   /* Error details per OCP spec */
   struct json_object *error_obj = json_object_new_object();
   json_object_object_add(error_obj, "code", json_object_new_string(error_code));
   json_object_object_add(error_obj, "message", json_object_new_string(error_message));
   json_object_object_add(response, "error", error_obj);

   const char *json_str = json_object_to_json_string(response);
   LOG_WARNING("OCP Error Response: %s", json_str);

   mqttSendMessage("dawn", json_str);
   json_object_put(response);
}

/**
 * Notifies the AI process that a "viewing" command has completed, providing a snapshot
 * filename for processing.
 * @deprecated Use mqttViewingSnapshotOCP for OCP-compliant responses
 */
void mqttViewingSnapshot(const char *filename) {
   /* Use empty request_id for backwards compatibility */
   mqttViewingSnapshotOCP(filename, NULL);
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
      char output_path[PATH_MAX + 31];
      char request_id[128] = "";

      /* Copy request_id to local variable */
      if (g_screenshot_request_id[0] != '\0') {
         strncpy(request_id, g_screenshot_request_id, sizeof(request_id) - 1);
         request_id[sizeof(request_id) - 1] = '\0';
      }

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
               snprintf(output_path, sizeof(output_path), "%s/snapshot-%s.jpg", base_path,
                        datetime);
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

         snprintf(output_path, sizeof(output_path), "%s/screenshot-%s.jpg", record_path, datetime);
      }

      /* Clear the request flag before taking the screenshot */
      g_screenshot_requested = 0;
      g_screenshot_path[0] = '\0';
      g_screenshot_source = SCREENSHOT_MANUAL;
      g_screenshot_request_id[0] = '\0';

      pthread_mutex_unlock(&g_screenshot_mutex);

      /* Now take the screenshot from the main thread where OpenGL context is valid */
      int result = take_screenshot(with_overlay, no_camera_mode, full_resolution, output_path);

      /* Send notification if it was an MQTT request */
      if (source == SCREENSHOT_MQTT) {
         if (result == 0) {
            LOG_INFO("Screenshot for MQTT. Sending with request_id=%s",
                     request_id[0] ? request_id : "(none)");
            mqttViewingSnapshotOCP(output_path, request_id[0] ? request_id : NULL);
         } else {
            LOG_ERROR("Screenshot capture failed (result=%d)", result);
            mqttViewingSnapshotErrorOCP(request_id[0] ? request_id : NULL, "CAPTURE_FAILED",
                                        "Failed to capture screenshot from camera");
         }
      }
   } else {
      pthread_mutex_unlock(&g_screenshot_mutex);
   }
}
