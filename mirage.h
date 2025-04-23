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

#ifndef MAIN_H
#define MAIN_H

#include <limits.h>
#include "SDL2/SDL.h"
#include "SDL2/SDL_ttf.h"
#include "config_parser.h"
#include "detect.h"
#include "devices.h"

// Device gets
enviro *get_enviro_dev(void);
motion *get_motion_dev(void);
gps *get_gps_dev(void);

// Element gets/set
element *get_default_element(void);
element *get_first_element(void);
element *get_intro_element(void);
element *set_first_element(element *this_element);

// Enable object detection overlay
int set_detect_enabled(int enable);

// Set the new AI info.
void process_ai_state(const char *newAIName, const char *newAIState);

// Supported recoding and streaming states
typedef enum {
   DISABLED=0,
   RECORD=1,
   STREAM=2,
   RECORD_STREAM=4
} DestinationType;

typedef struct _video_out_data {
   DestinationType output;
   pthread_mutex_t p_mutex;
   int buffer_num;

   void *rgb_out_pixels[2];

   char filename[PATH_MAX+64];
   int started;
   FILE *outfile;
} video_out_data;

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

// Set recording state.
void set_recording_state(DestinationType state);

// Get SDL renderer for direct access.
SDL_Renderer *get_sdl_renderer(void);

// Get font from local cache.
TTF_Font *get_local_font(char *font_name, int font_size);

// The quit variable is stored in main.c, this gets its state.
int checkShutdown(void);

// Render a given texture to the given location in the stereo display.
void renderStereo(SDL_Texture * tex, SDL_Rect * src, SDL_Rect * dest, SDL_Rect * dest2,
                  double angle);

// Send the given text over mqtt to be send to audio.
void mqttTextToSpeech(const char *text);

// Trigger a one-time snapshot.
void trigger_snapshot(const char *datetime);

#endif // DEFINES_H

