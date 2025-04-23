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

#ifndef HUD_MANAGER_H
#define HUD_MANAGER_H

#include "SDL2/SDL.h"
#include "config_parser.h"

/* Transition type constants */
typedef enum {
   TRANSITION_FADE = 0,
   TRANSITION_SLIDE_LEFT,
   TRANSITION_SLIDE_RIGHT,
   TRANSITION_ZOOM,
   TRANSITION_MAX
} transition_t;

/* HUD screen definition */
typedef struct _hud_screen {
   char name[MAX_TEXT_LENGTH];        /* Name of this HUD configuration */
   char hotkey[2];                    /* Optional hotkey to activate this HUD */
   transition_t transition_type;      /* Optional transition specific to this HUD */
   int hud_id;                        /* Unique identifier for this HUD (0-15) */
   struct _hud_screen *next;          /* Next HUD in chain */
} hud_screen;

/* HUD management */
typedef struct _hud_manager {
   hud_screen *screens;               /* List of available HUD screens */
   hud_screen *current_screen;        /* Currently active screen */
   hud_screen *transition_from;       /* Screen transitioning from (NULL if not in transition) */
   float transition_progress;         /* 0.0-1.0 progress of current transition */
   transition_t transition_type;      /* Type of transition animation */
   int transition_duration_ms;        /* Duration in milliseconds */
   Uint32 transition_start_time;      /* When the transition started */
} hud_manager;

/* Initialization and cleanup functions */
void init_hud_manager(void);
void cleanup_hud_manager(void);

/* HUD management functions */
hud_screen* find_hud_by_name(const char* name);
hud_screen* find_hud_by_id(int id);
void switch_to_hud(const char *hud_name, transition_t transition_type, int transition_duration_ms);
int register_hud(const char *name, const char *hotkey, const char *transition);
int get_current_hud_id(void);
hud_manager* get_hud_manager(void);

/* Rendering functions */
const char* get_transition_name(transition_t transition_type);
int find_transition_by_name(const char* name);

#endif /* HUD_MANAGER_H */
