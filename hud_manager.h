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

/**
 * @brief Initializes the HUD manager system.
 *
 * This function initializes the global HUD manager structure, setting all pointers
 * to NULL and establishing default transition settings. Must be called before
 * using any other HUD manager functions.
 */
void init_hud_manager(void);

/**
 * @brief Cleans up HUD manager resources.
 *
 * This function frees all allocated memory for HUD screens and resets the
 * HUD manager to its initial state. Should be called during application shutdown.
 */
void cleanup_hud_manager(void);

/* HUD management functions */

/**
 * @brief Finds a HUD screen by its name.
 *
 * This function searches through the linked list of registered HUD screens
 * to find one with a matching name.
 *
 * @param name The name of the HUD to search for
 * @return Pointer to the hud_screen structure if found, NULL otherwise
 */
hud_screen* find_hud_by_name(const char* name);

/**
 * @brief Finds a HUD screen by its ID.
 *
 * This function searches through the linked list of registered HUD screens
 * to find one with a matching ID.
 *
 * @param id The ID of the HUD to search for (0-15)
 * @return Pointer to the hud_screen structure if found, NULL otherwise
 */
hud_screen* find_hud_by_id(int id);

/**
 * @brief Switches to a different HUD with specified transition.
 *
 * This function initiates a transition from the current HUD to the specified
 * target HUD using the given transition type and duration. If the target HUD
 * is the same as the current HUD, no action is taken.
 *
 * @param hud_name Name of the target HUD to switch to
 * @param transition_type Type of transition animation to use
 * @param transition_duration_ms Duration of the transition in milliseconds
 */
void switch_to_hud(const char *hud_name, transition_t transition_type, int transition_duration_ms);

/**
 * @brief Switches to the next HUD in the sequence.
 *
 * This function cycles through the available HUDs in order. If currently on the last HUD,
 * it wraps around to the first HUD. Uses the default transition type and duration.
 */
void switch_to_next_hud(void);

/**
 * @brief Registers a new HUD with the HUD manager.
 *
 * This function creates and registers a new HUD screen with the specified parameters.
 * Each HUD is assigned a unique ID automatically. The maximum number of HUDs is
 * limited by MAX_HUDS.
 *
 * @param name The name identifier for the HUD (must be unique)
 * @param hotkey Optional hotkey string to activate this HUD (can be NULL)
 * @param transition Optional default transition type for this HUD (can be NULL)
 * @return The assigned HUD ID (0-15) on success, -1 on failure
 */
int register_hud(const char *name, const char *hotkey, const char *transition);

/**
 * @brief Gets the ID of the currently active HUD.
 *
 * This function returns the unique identifier of the HUD that is currently
 * being displayed or being transitioned to.
 *
 * @return The current HUD ID (0-15), or -1 if no HUD is active
 */
int get_current_hud_id(void);

/**
 * @brief Gets a pointer to the global HUD manager structure.
 *
 * This function provides direct access to the HUD manager for advanced
 * operations such as checking transition state.
 *
 * @return Pointer to the global hud_manager structure
 */
hud_manager* get_hud_manager(void);

/* Rendering functions */

/**
 * @brief Gets the string name of a transition type.
 *
 * This function converts a transition_t enumeration value to its corresponding
 * string representation for logging and configuration purposes.
 *
 * @param transition_type The transition type enumeration value
 * @return String name of the transition type, or "unknown" for invalid values
 */
const char* get_transition_name(transition_t transition_type);

/**
 * @brief Finds a transition type by its string name.
 *
 * This function searches for a transition type that matches the given string name.
 * Used for parsing configuration files and user input.
 *
 * @param name The string name of the transition type to find
 * @return The corresponding transition_t value, or the default transition type if not found
 */
int find_transition_by_name(const char* name);

#endif /* HUD_MANAGER_H */
