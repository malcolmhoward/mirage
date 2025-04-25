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

#ifndef ENVIRONMENTAL_ELEMENT_H
#define ENVIRONMENTAL_ELEMENT_H

#include "SDL2/SDL.h"
#include "config_parser.h"

/**
 * @brief Renders the environmental panel element
 * 
 * This function renders a dynamic environmental data visualization panel
 * that displays temperature, air quality, CO2 levels, humidity, VOC levels,
 * and heat index information using primitive drawing elements.
 * 
 * @param curr_element Pointer to the element structure containing position and dimensions
 */
void render_environmental_panel_element(element *curr_element);

/**
 * @brief Cleans up resources used by the environmental panel
 * 
 * This function should be called during program shutdown to release
 * any textures or other resources used by the environmental panel.
 */
void cleanup_environmental_panel(void);

#endif /* ENVIRONMENTAL_ELEMENT_H */
