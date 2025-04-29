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

#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <SDL2/SDL.h>
#include <limits.h>

/* Type definition for screenshot source */
typedef enum {
   SCREENSHOT_MANUAL = 0,
   SCREENSHOT_MQTT
} screenshot_t;

/* Function to initialize the PBO system for async pixel reads */
void init_pbo_system(void);

/* Function to clean up the PBO system */
void cleanup_pbo_system(void);

/**
 * Asynchronously reads pixels from the current OpenGL framebuffer into a user buffer
 *
 * @param renderer Pointer to the SDL_Renderer
 * @param rect Optional rectangle specifying the area to read
 * @param format SDL pixel format
 * @param pixels Pointer to the user-allocated buffer
 * @param pitch Byte pitch of the user buffer
 * @return 0 on success, 1 on failure
 */
int OpenGL_RenderReadPixelsAsync(SDL_Renderer *renderer,
                                const SDL_Rect *rect,
                                Uint32 format,
                                void *pixels,
                                int pitch);

/**
 * Requests a screenshot to be taken by the main thread
 *
 * @param with_overlay Whether to include UI overlay
 * @param full_resolution Whether to maintain full resolution
 * @param output_filename Path to save the screenshot (or NULL for auto-generated)
 * @param source The source of the screenshot request
 * @return 0 if request was queued successfully, non-zero otherwise
 */
int request_screenshot(int with_overlay, int full_resolution,
                       const char *output_filename, screenshot_t source);

/**
 * Takes a screenshot with specified options
 *
 * @param with_overlay If true, captures with UI overlay
 * @param full_resolution If true, maintains original resolution
 * @param output_filename Optional custom filename
 * @return 0 on success, non-zero on failure
 */
int take_screenshot(int with_overlay, int full_resolution, const char *output_filename);

/**
 * Takes a snapshot for AI processing
 *
 * @param datetime String containing datetime for filename (optional)
 */
void trigger_snapshot(const char *datetime);

/**
 * Process any pending screenshot requests in the main thread
 */
void process_screenshot_requests(void);

/**
 * Sends notification that a viewing snapshot has been completed
 *
 * @param filename The filename of the completed snapshot
 */
void mqttViewingSnapshot(const char *filename);

/* Set the recording path directory */
void set_screenshot_recording_path(const char *path);

#endif /* SCREENSHOT_H */
