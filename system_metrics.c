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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include "system_metrics.h"
#include "logging.h"

/* Global metrics structure instance */
system_metrics_t system_metrics = {
   .cpu_usage = 0.0f,
   .memory_usage = 0.0f,
   .fan_rpm = -1,
   .fan_load = -1,
   .power_voltage = 0.0f,
   .power_current = 0.0f,
   .power_consumption = 0.0f,
   .power_temperature = 0.0f,
   .battery_level = 0.0f,
   .battery_status = "UNKNOWN",
   .time_remaining_min = 0.0f,
   .time_remaining_fmt = "0:00",
   .battery_chemistry = "UNKN",
   .battery_capacity_mah = 0.0f,
   .battery_cells = 0,

   .cpu_update_time = 0,
   .memory_update_time = 0,
   .fan_update_time = 0,
   .power_update_time = 0,

   .cpu_available = false,
   .memory_available = false,
   .fan_available = false,
   .power_available = false
};

/**
 * @brief Initialize system metrics with default values
 */
void init_system_metrics(void)
{
   /* Reset all metrics to default values */
   system_metrics.cpu_usage = 0.0f;
   system_metrics.memory_usage = 0.0f;
   system_metrics.fan_rpm = -1;
   system_metrics.fan_load = -1;
   system_metrics.power_voltage = 0.0f;
   system_metrics.power_current = 0.0f;
   system_metrics.power_consumption = 0.0f;
   system_metrics.power_temperature = 0.0f;
   system_metrics.battery_level = 0.0f;
   strcpy(system_metrics.battery_status, "UNKNOWN");
   system_metrics.time_remaining_min = 0.0f;
   strcpy(system_metrics.time_remaining_fmt, "0:00");
   strcpy(system_metrics.battery_chemistry, "UNKN");
   system_metrics.battery_capacity_mah = 0.0f;
   system_metrics.battery_cells = 0;
   
   /* Reset all timestamps */
   time_t current_time = time(NULL);
   system_metrics.cpu_update_time = current_time;
   system_metrics.memory_update_time = current_time;
   system_metrics.fan_update_time = current_time;
   system_metrics.power_update_time = current_time;
   
   /* Set all metrics as unavailable initially */
   system_metrics.cpu_available = false;
   system_metrics.memory_available = false;
   system_metrics.fan_available = false;
   system_metrics.power_available = false;
   
   LOG_INFO("System metrics initialized");
}

/**
 * @brief Check if a metric is stale (not updated recently)
 * 
 * @param update_time The last update timestamp for the metric
 * @param timeout_seconds The number of seconds after which a metric is considered stale
 * @return true if the metric is stale, false otherwise
 */
bool is_metric_stale(time_t update_time, int timeout_seconds)
{
   time_t current_time = time(NULL);
   return (current_time - update_time) > timeout_seconds;
}

/**
 * @brief Update the system metrics based on metric staleness
 * 
 * This function checks if any metrics are stale and updates their
 * availability flags accordingly.
 * 
 * @param timeout_seconds The number of seconds after which a metric is considered stale
 */
void update_metrics_availability(int timeout_seconds)
{
   /* Check each metric and update its availability */
   system_metrics.cpu_available = !is_metric_stale(system_metrics.cpu_update_time, timeout_seconds);
   system_metrics.memory_available = !is_metric_stale(system_metrics.memory_update_time, timeout_seconds);
   system_metrics.fan_available = !is_metric_stale(system_metrics.fan_update_time, timeout_seconds);
   system_metrics.power_available = !is_metric_stale(system_metrics.power_update_time, timeout_seconds);
   
   /* Log if metrics become unavailable */
#if 0 /* Suppressing these for now. */
   if (!system_metrics.cpu_available) {
      LOG_WARNING("CPU metrics have become stale (not updated in %d seconds)", timeout_seconds);
   }
   
   if (!system_metrics.memory_available) {
      LOG_WARNING("Memory metrics have become stale (not updated in %d seconds)", timeout_seconds);
   }
   
   if (!system_metrics.fan_available) {
      LOG_WARNING("Fan metrics have become stale (not updated in %d seconds)", timeout_seconds);
   }
   
   if (!system_metrics.power_available) {
      LOG_WARNING("Power metrics have become stale (not updated in %d seconds)", timeout_seconds);
   }
#endif
}

/**
 * @brief Get CPU usage percentage
 * 
 * @return float CPU usage percentage or -1.0f if unavailable
 */
float get_cpu_usage(void)
{
   if (system_metrics.cpu_available) {
      return system_metrics.cpu_usage;
   } else {
      return -1.0f;
   }
}

/**
 * @brief Get memory usage percentage
 * 
 * @return float Memory usage percentage or -1.0f if unavailable
 */
float get_memory_usage(void)
{
   if (system_metrics.memory_available) {
      return system_metrics.memory_usage;
   } else {
      return -1.0f;
   }
}

/**
 * @brief Get fan RPM
 * 
 * @return int Fan RPM or -1 if unavailable
 */
int get_fan_rpm(void)
{
   if (system_metrics.fan_available) {
      return system_metrics.fan_rpm;
   } else {
      return -1;
   }
}

/**
 * @brief Get fan load percentage
 * 
 * @return int Fan load percentage or -1 if unavailable
 */
int get_fan_load_percent(void)
{
   if (system_metrics.fan_available) {
      return system_metrics.fan_load;
   } else {
      return -1;
   }
}

