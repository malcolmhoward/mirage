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

#ifndef FAN_MONITORING_H
#define FAN_MONITORING_H

/**
 * Initializes the fan monitoring subsystem by finding the RPM file path.
 * 
 * @return 0 on success, -1 on failure
 */
int init_fan_monitoring(void);

/**
 * Sets the maximum expected RPM value for the fan.
 * Used for scaling the RPM to a percentage.
 * 
 * @param max_rpm The maximum RPM value expected
 */
void set_fan_max_rpm(int max_rpm);

/**
 * Gets the current fan RPM.
 * 
 * @return The current RPM value, or -1 if unavailable
 */
int get_fan_rpm(void);

/**
 * Gets the fan load as a percentage (0-100).
 * 
 * @return Percentage of fan's maximum RPM, or -1 if unavailable
 */
int get_fan_load_percent(void);

/**
 * Cleans up fan monitoring resources.
 */
void cleanup_fan_monitoring(void);

#endif /* FAN_MONITORING_H */

