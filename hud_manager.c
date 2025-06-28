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

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "config_manager.h"
#include "hud_manager.h"
#include "logging.h"
#include "mirage.h"

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
void switch_to_hud(hud_screen *this_screen, transition_t transition_type) {
   if (this_screen == NULL) {
      LOG_ERROR("Invalid HD passed for transition.");
      return;
   }
   
   if (this_screen == hud_mgr.current_screen) {
      LOG_INFO("Already on HUD '%s'", this_screen->name);
      return;
   }
   
   /* Validate transition type */
   if (transition_type < 0 || transition_type >= TRANSITION_MAX) {
      LOG_WARNING("Invalid transition type %d, using default for screen %s",
                  transition_type, this_screen->transition_type);
      transition_type = this_screen->transition_type;
   }

   /* Start transition */
   hud_mgr.transition_from = hud_mgr.current_screen;
   hud_mgr.current_screen = this_screen;
   hud_mgr.transition_progress = 0.0;
   hud_mgr.transition_type = transition_type;
   //hud_mgr.transition_duration_ms = transition_duration_ms;
   hud_mgr.transition_start_time = SDL_GetTicks();
   
   LOG_INFO("Switching to HUD: %s with transition %s", 
            this_screen->name, get_transition_name(transition_type));
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

/**
 * @brief Switches to the next HUD in the sequence.
 *
 * This function cycles through the available HUDs in order. If currently on the last HUD,
 * it wraps around to the first HUD. Uses the default transition type and duration.
 */
void switch_to_next_hud(void) {
   hud_manager *hud_mgr = get_hud_manager();

   if (hud_mgr == NULL || hud_mgr->current_screen == NULL) {
      LOG_ERROR("No HUD manager or current screen available");
      return;
   }

   hud_screen *next_screen = hud_mgr->current_screen->next;

   // If we're at the end of the list, wrap around to the first screen
   if (next_screen == NULL) {
      next_screen = hud_mgr->screens;
   }

   // If we still don't have a next screen, there's only one HUD or none
   if (next_screen == NULL) {
      LOG_WARNING("No other HUDs available to switch to");
      return;
   }

   // Use the existing switch function with default transition settings
   switch_to_hud(next_screen, next_screen->transition_type);
}
