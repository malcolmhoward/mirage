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

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "defines.h"
#include "fan_monitoring.h"
#include "logging.h"

static char fan_rpm_path[PATH_MAX] = "";
static int fan_monitoring_initialized = 0;
static int fan_max_rpm = FAN_MAX_RPM;
static FILE *rpm_file = NULL;

/**
 * Finds the fan RPM file on Linux systems, with specific support for Jetson.
 * 
 * @param rpm_path Buffer to store the RPM file path
 * @param path_size Size of the buffer
 * @return 0 on success, -1 if not found
 */
static int find_fan_rpm_file(char *rpm_path, size_t path_size) {
   DIR *dir;
   struct dirent *entry;
   char path[PATH_MAX];
   char test_path[PATH_MAX+11];
   int found = 0;
   
   // 1. Try the Jetson tachometer path first (most specific)
   dir = opendir("/sys/devices/platform");
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         if (strstr(entry->d_name, "bus@0") || strstr(entry->d_name, "tachometer")) {
            snprintf(path, sizeof(path), "/sys/devices/platform/%s", entry->d_name);
            DIR *sub_dir = opendir(path);
            if (sub_dir) {
               struct dirent *sub_entry;
               while ((sub_entry = readdir(sub_dir)) != NULL) {
                  if (strstr(sub_entry->d_name, "tachometer")) {
                     // Found tachometer directory, now look for hwmon
                     snprintf(path, sizeof(path), "/sys/devices/platform/%s/%s/hwmon", 
                              entry->d_name, sub_entry->d_name);
                     DIR *hwmon_dir = opendir(path);
                     if (hwmon_dir) {
                        struct dirent *hwmon_entry;
                        while ((hwmon_entry = readdir(hwmon_dir)) != NULL) {
                           if (strstr(hwmon_entry->d_name, "hwmon")) {
                              snprintf(rpm_path, path_size, "%s/%s/rpm", 
                                       path, hwmon_entry->d_name);
                              if (access(rpm_path, R_OK) == 0) {
                                 found = 1;
                                 LOG_INFO("Found tachometer RPM file: %s", rpm_path);
                                 break;
                              }
                           }
                        }
                        closedir(hwmon_dir);
                        if (found) break;
                     }
                  }
               }
               closedir(sub_dir);
               if (found) break;
            }
         }
      }
      closedir(dir);
      if (found) return 0;
   }
   
   // 2. Try the hwmon class (generic approach)
   dir = opendir("/sys/class/hwmon");
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         if (entry->d_name[0] == '.') continue;
         
         // Check for direct rpm file
         snprintf(test_path, sizeof(test_path), "/sys/class/hwmon/%s/rpm", entry->d_name);
         if (access(test_path, R_OK) == 0) {
            strncpy(rpm_path, test_path, path_size);
            rpm_path[path_size - 1] = '\0';
            LOG_INFO("Found RPM file in hwmon: %s", rpm_path);
            return 0;
         }
         
         // Check for fan input files
         for (int i = 1; i <= 5; i++) {
            snprintf(test_path, sizeof(test_path), "/sys/class/hwmon/%s/fan%d_input", 
                     entry->d_name, i);
            if (access(test_path, R_OK) == 0) {
               strncpy(rpm_path, test_path, path_size);
               rpm_path[path_size - 1] = '\0';
               LOG_INFO("Found fan input file in hwmon: %s", rpm_path);
               return 0;
            }
         }
         
         // Check for device subdirectory
         snprintf(path, sizeof(path), "/sys/class/hwmon/%s/device", entry->d_name);
         DIR *device_dir = opendir(path);
         if (device_dir) {
            // Check for rpm in device subdir
            snprintf(test_path, sizeof(test_path), "%s/rpm", path);
            if (access(test_path, R_OK) == 0) {
               strncpy(rpm_path, test_path, path_size);
               rpm_path[path_size - 1] = '\0';
               LOG_INFO("Found RPM file in hwmon device: %s", rpm_path);
               closedir(device_dir);
               return 0;
            }
            
            // Check for fan inputs in device subdir
            for (int i = 1; i <= 5; i++) {
               snprintf(test_path, sizeof(test_path), "%s/fan%d_input", path, i);
               if (access(test_path, R_OK) == 0) {
                  strncpy(rpm_path, test_path, path_size);
                  rpm_path[path_size - 1] = '\0';
                  LOG_INFO("Found fan input in hwmon device: %s", rpm_path);
                  closedir(device_dir);
                  return 0;
               }
            }
            closedir(device_dir);
         }
      }
      closedir(dir);
   }
   
   // 3. Try direct paths for common locations (fallback)
   const char *common_paths[] = {
      "/sys/devices/platform/pwm-fan/hwmon/hwmon0/rpm",
      "/sys/devices/platform/pwm-fan/hwmon/hwmon1/rpm",
      "/sys/devices/platform/pwm-fan/hwmon/hwmon2/rpm",
      "/sys/devices/platform/pwm-fan/hwmon/hwmon3/rpm",
      "/sys/devices/platform/pwm-fan/hwmon/hwmon4/rpm",
      "/sys/devices/platform/pwm-fan/hwmon/hwmon5/rpm",
      NULL
   };
   
   for (int i = 0; common_paths[i] != NULL; i++) {
      if (access(common_paths[i], R_OK) == 0) {
         strncpy(rpm_path, common_paths[i], path_size);
         rpm_path[path_size - 1] = '\0';
         LOG_INFO("Found RPM file at common path: %s", rpm_path);
         return 0;
      }
   }
   
   LOG_ERROR("Could not find fan RPM file");
   return -1; // Not found
}

/**
 * Initializes the fan monitoring subsystem by finding the RPM file path
 * and opening the file for reading.
 *
 * @return 0 on success, -1 on failure
 */
int init_fan_monitoring(void) {
   if (fan_monitoring_initialized && rpm_file != NULL) {
      return 0; // Already initialized with valid file
   }

   // Close previous file if open
   if (rpm_file != NULL) {
      fclose(rpm_file);
      rpm_file = NULL;
   }

   if (find_fan_rpm_file(fan_rpm_path, sizeof(fan_rpm_path)) != 0) {
      LOG_WARNING("Failed to find fan RPM file, fan monitoring disabled");
      return -1;
   }

   // Open the file for persistent use
   rpm_file = fopen(fan_rpm_path, "r");
   if (rpm_file == NULL) {
      LOG_ERROR("Failed to open fan RPM file: %s", fan_rpm_path);
      return -1;
   }

   LOG_INFO("Fan monitoring initialized with RPM file: %s", fan_rpm_path);
   fan_monitoring_initialized = 1;
   return 0;
}

/**
 * Sets the maximum expected RPM value for the fan.
 * Used for scaling the RPM to a percentage.
 * 
 * @param max_rpm The maximum RPM value expected
 */
void set_fan_max_rpm(int max_rpm) {
   if (max_rpm > 0) {
      fan_max_rpm = max_rpm;
      LOG_INFO("Fan max RPM set to %d", fan_max_rpm);
   }
}

/**
 * Gets the current fan RPM.
 * 
 * @return The current RPM value, or -1 if unavailable
 */
int get_fan_rpm(void) {
   int rpm = -1;
   
   if (!fan_monitoring_initialized || rpm_file == NULL) {
      if (init_fan_monitoring() != 0) {
         return -1;
      }
   }
   
   // Reset error indicators
   clearerr(rpm_file);
   
   // Go to the beginning of the file
   rewind(rpm_file);
   
   // Read the RPM value
   if (fscanf(rpm_file, "%d", &rpm) != 1) {
      LOG_WARNING("Failed to read fan RPM value, attempting to reinitialize");
      
      // Try to reinitialize
      fan_monitoring_initialized = 0;
      fclose(rpm_file);
      rpm_file = NULL;
      
      if (init_fan_monitoring() == 0) {
         // Try again with new file
         rewind(rpm_file);
         if (fscanf(rpm_file, "%d", &rpm) != 1) {
            rpm = -1;
         }
      }
   }
   
   return rpm;
}

/**
 * Gets the fan load as a percentage (0-100).
 * 
 * @return Percentage of fan's maximum RPM, or -1 if unavailable
 */
int get_fan_load_percent(void) {
   int rpm = get_fan_rpm();
   if (rpm < 0) return -1;
   
   int percent = (rpm * 100) / fan_max_rpm;
   if (percent > 100) percent = 100; // Cap at 100%
   
   return percent;
}

/**
 * Cleans up fan monitoring resources.
 */
void cleanup_fan_monitoring(void) {
   if (rpm_file != NULL) {
      fclose(rpm_file);
      rpm_file = NULL;
   }
   
   fan_monitoring_initialized = 0;
   fan_rpm_path[0] = '\0';
   LOG_INFO("Fan monitoring cleaned up");
}

