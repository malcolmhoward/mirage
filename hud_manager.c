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
#include <string.h>
#include <stdlib.h>

#include "SDL2/SDL.h"
#include "hud_manager.h"
#include "logging.h"
#include "mirage.h"
#include "config_manager.h"

/* Global HUD manager instance */
static hud_manager hud_mgr = {
   .screens = NULL,
   .current_screen = NULL,
   .transition_from = NULL,
   .transition_progress = 0.0,
   .transition_type = TRANSITION_FADE,
   .transition_duration_ms = 500,
   .transition_start_time = 0
};

/* Transition names for user-friendly configuration */
static const char* transition_names[TRANSITION_MAX] = {
   "fade",
   "slide_left",
   "slide_right",
   "zoom"
};

/* Initialize HUD manager */
void init_hud_manager(void) {
   hud_mgr.screens = NULL;
   hud_mgr.current_screen = NULL;
   hud_mgr.transition_from = NULL;
   hud_mgr.transition_progress = 0.0;
   hud_mgr.transition_type = TRANSITION_FADE;
   hud_mgr.transition_duration_ms = 500;
}

/* Clean up HUD manager resources */
void cleanup_hud_manager(void) {
   hud_screen *current = hud_mgr.screens;
   while (current != NULL) {
      hud_screen *next = current->next;
      free(current);
      current = next;
   }
   hud_mgr.screens = NULL;
   hud_mgr.current_screen = NULL;
}

/* Find a HUD by name */
hud_screen* find_hud_by_name(const char* name) {
   hud_screen* current = hud_mgr.screens;
   while (current != NULL) {
      if (strcmp(current->name, name) == 0) {
         return current;
      }
      current = current->next;
   }
   return NULL;
}

/* Find a HUD by ID */
hud_screen* find_hud_by_id(int id) {
   hud_screen* current = hud_mgr.screens;
   while (current != NULL) {
      if (current->hud_id == id) {
         return current;
      }
      current = current->next;
   }
   return NULL;
}

/* Register a new HUD */
int register_hud(const char *name, const char *hotkey, const char *transition) {
   if (find_hud_by_name(name) != NULL) {
      LOG_ERROR("HUD with name %s already exists", name);
      return -1;
   }
   
   /* Find next available ID */
   int next_id = 0;
   hud_screen *current = hud_mgr.screens;
   while (current != NULL) {
      if (current->hud_id >= next_id) {
         next_id = current->hud_id + 1;
      }
      if (next_id >= MAX_HUDS) {
         LOG_ERROR("Maximum number of HUDs reached (%d)", MAX_HUDS);
         return -1;
      }
      current = current->next;
   }
   
   /* Create new HUD screen */
   hud_screen *new_screen = (hud_screen *)malloc(sizeof(hud_screen));
   if (new_screen == NULL) {
      LOG_ERROR("Failed to allocate memory for new HUD");
      return -1;
   }
   
   /* Initialize the HUD */
   strncpy(new_screen->name, name, MAX_TEXT_LENGTH-1);
   new_screen->name[MAX_TEXT_LENGTH-1] = '\0';
   
   if (hotkey != NULL) {
      strncpy(new_screen->hotkey, hotkey, 1);
      new_screen->hotkey[1] = '\0';
   } else {
      new_screen->hotkey[0] = '\0';
   }

   new_screen->transition_type = find_transition_by_name(transition);
   
   new_screen->hud_id = next_id;
   new_screen->next = NULL;
   
   /* Add to HUD list */
   if (hud_mgr.screens == NULL) {
      hud_mgr.screens = new_screen;
      hud_mgr.current_screen = new_screen; /* First HUD becomes default */
   } else {
      /* Add to end of list */
      current = hud_mgr.screens;
      while (current->next != NULL) {
         current = current->next;
      }
      current->next = new_screen;
   }
   
   return new_screen->hud_id;
}

/* Switch to a different HUD with specified transition */
void switch_to_hud(const char *hud_name, transition_t transition_type, int transition_duration_ms) {
   hud_screen *target = find_hud_by_name(hud_name);
   
   if (target == NULL) {
      LOG_ERROR("HUD '%s' not found", hud_name);
      return;
   }
   
   if (target == hud_mgr.current_screen) {
      LOG_INFO("Already on HUD '%s'", hud_name);
      return;
   }
   
   /* Validate transition type */
   if (transition_type < 0 || transition_type >= TRANSITION_MAX) {
      LOG_WARNING("Invalid transition type %d, using default fade", transition_type);
      transition_type = TRANSITION_FADE;
   }

   /* Validate transition duration */
   if (transition_duration_ms <= 0) {
      LOG_WARNING("Invalid transition duration %d, using default 500ms", transition_duration_ms);
      transition_duration_ms = 500;
   }
   
   /* Start transition */
   hud_mgr.transition_from = hud_mgr.current_screen;
   hud_mgr.current_screen = target;
   hud_mgr.transition_progress = 0.0;
   hud_mgr.transition_type = transition_type;
   hud_mgr.transition_duration_ms = transition_duration_ms;
   hud_mgr.transition_start_time = SDL_GetTicks();
   
   LOG_INFO("Switching to HUD: %s with transition %s (%dms)", 
            hud_name, get_transition_name(transition_type), transition_duration_ms);
}

/* Get ID of current HUD */
int get_current_hud_id(void) {
   if (hud_mgr.current_screen == NULL) {
      return -1;
   }
   return hud_mgr.current_screen->hud_id;
}

/* Get pointer to the HUD manager */
hud_manager* get_hud_manager(void) {
   return &hud_mgr;
}

/* Get string name of transition type */
const char* get_transition_name(transition_t transition_type) {
   if (transition_type >= 0 && transition_type < TRANSITION_MAX) {
      return transition_names[transition_type];
   }
   return "unknown";
}

/* Find transition type by name */
int find_transition_by_name(const char* name) {
   if (name == NULL) {
      return get_hud_manager()->transition_type; /* Default if not defined */
   }

   for (int i = 0; i < TRANSITION_MAX; i++) {
      if (strcmp(name, transition_names[i]) == 0) {
         return i;
      }
   }
   return get_hud_manager()->transition_type; /* Default if not found */
}

