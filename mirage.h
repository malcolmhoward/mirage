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

#ifndef MIRAGE_H
#define MIRAGE_H

#include <gst/gst.h>
#include <limits.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "config_parser.h"
#include "detect.h"
#include "devices.h"
#include "recording.h"

// DATA Structures and Defines

/* Object detection data, one for each eye. */
typedef struct _od_data {
   detect_net detect_obj;
   void *pix_data;

   int eye;

   int complete;
   int processed;
} od_data;

/* ALERTS */
typedef enum {
   ALERT_NONE        = 0,
   ALERT_RECORDING   = 1 << 0,
   ALERT_MAX         = 1 << 1
} alert_t;

struct Alert {
    alert_t flag;
    const char* message;
};

// Device gets

/**
 * @brief Returns a pointer to the global motion data structure.
 *
 * This function provides access to the motion sensor data including
 * heading, pitch, roll and quaternion values.
 *
 * @return Pointer to the global motion data structure.
 */
motion *get_motion_dev(void);

/**
 * @brief Returns a pointer to the global environmental data structure.
 *
 * This function provides access to environmental sensor data including
 * temperature, humidity, air quality, CO2 levels, and related metrics.
 *
 * @return Pointer to the global environmental data structure.
 */
enviro *get_enviro_dev(void);

/**
 * @brief Returns a pointer to the global GPS data structure.
 *
 * This function provides access to GPS data including position coordinates,
 * time, date, speed, altitude, and satellite information.
 *
 * @return Pointer to the global GPS data structure.
 */
gps *get_gps_dev(void);

// Element gets/seta

/**
 * @brief Returns a pointer to the default element template.
 *
 * This function provides access to the default element structure
 * that serves as a template for creating new UI elements.
 *
 * @return Pointer to the default element template.
 */
element *get_default_element(void);

/**
 * @brief Returns a pointer to the first element in the UI element linked list.
 *
 * This function provides access to the head of the UI element linked list,
 * allowing iteration through all UI elements.
 *
 * @return Pointer to the first UI element, or NULL if the list is empty.
 */
element *get_first_element(void);

/**
 * @brief Returns a pointer to the global intro element.
 *
 * This function provides access to the intro animation element that is
 * displayed during system startup.
 *
 * @return Pointer to the global intro element.
 */
element *get_intro_element(void);

/**
 * @brief Sets the first element in the UI element linked list.
 *
 * This function updates the head pointer of the UI element linked list
 * to the specified element.
 *
 * @param this_element Pointer to the element that will become the first element.
 * @return Pointer to the new first element.
 */
element *set_first_element(element *this_element);

/**
 * @brief Enables or disables object detection.
 *
 * This function controls whether object detection is active in the system.
 *
 * @param enable Non-zero to enable detection, zero to disable.
 * @return The new state of the detect_enabled flag.
 */
int set_detect_enabled(int enable);

/**
 * @brief Returns a pointer to the global SDL renderer.
 *
 * This function provides access to the SDL renderer used for all
 * rendering operations throughout the application. The renderer
 * should only be accessed from the main thread.
 *
 * @warning This function must only be called from the main thread
 * @return Pointer to the global SDL renderer, or NULL if called from a non-main thread
 */
SDL_Renderer *get_sdl_renderer(void);

/**
 * @brief Updates the AI assistant name and state.
 *
 * This function updates the global variables storing the AI assistant's
 * name and current state. These values are displayed in the UI and affect
 * which textures are used for the AI visualization.
 *
 * @param newAIName The name of the AI assistant to display.
 * @param newAIState The current state of the AI assistant (e.g., "SILENCE",
 *                   "WAKEWORD_LISTEN", "COMMAND_RECORDING", "PROCESS_COMMAND").
 */
void process_ai_state(const char *newAIName, const char *newAIState);

/**
 * @brief Gets the current window dimensions.
 *
 * This function retrieves the current width and height of the application window.
 *
 * @param[out] width Pointer to an integer that will receive the window width.
 * @param[out] height Pointer to an integer that will receive the window height.
 * @return SUCCESS on success, FAILURE on error.
 */
int get_window_size(int *width, int *height);

/**
 * @brief Retrieves a font from the font cache or loads it if not present.
 *
 * This function manages a cache of loaded TTF fonts to avoid repeatedly loading
 * the same font resources. If the requested font is already in the cache, it is
 * returned; otherwise, it is loaded from disk, added to the cache, and returned.
 *
 * @param font_name Path to the font file.
 * @param font_size Size of the font in points.
 * @return Pointer to the loaded TTF_Font, or NULL if loading failed.
 */
TTF_Font *get_local_font(char *font_name, int font_size);

/**
 * @brief Checks if the application is in the process of shutting down.
 *
 * This function returns the state of the global quit flag to determine
 * if threads should terminate their processing.
 *
 * @return Non-zero if the application is shutting down, zero otherwise.
 */
int checkShutdown(void);

/**
 * @brief Renders a texture to both eyes in a stereo display.
 *
 * This function handles the complexities of rendering textures for a stereo VR/AR
 * display. It applies appropriate transformations for each eye, handles cropping
 * for elements that extend beyond screen boundaries, and applies rotation if needed.
 *
 * @param tex The SDL texture to render.
 * @param src Source rectangle defining which portion of the texture to use (can be NULL for entire texture).
 * @param dest Destination rectangle for the left eye view.
 * @param dest2 Destination rectangle for the right eye view (can be NULL to use same as left).
 * @param angle Rotation angle in degrees (0 for no rotation).
 */
void renderStereo(SDL_Texture * tex, SDL_Rect * src, SDL_Rect * dest, SDL_Rect * dest2,
                  double angle);

/**
 * @brief Sends a text message to be spoken via text-to-speech over MQTT.
 *
 * This function constructs a JSON command with the provided text and publishes it
 * to the MQTT topic "dawn" for processing by a text-to-speech service.
 *
 * @param text The text string to be converted to speech.
 */
void mqttTextToSpeech(const char *text);

#endif // MIRAGE_H

