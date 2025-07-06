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

#ifndef SYSTEM_METRICS_H
#define SYSTEM_METRICS_H

#include <time.h>
#include <stdbool.h>

/* Structure for system metrics received from STAT */
typedef struct {
   float cpu_usage;            /* CPU usage percentage (0-100) */
   float memory_usage;         /* Memory usage percentage (0-100) */
   int fan_rpm;                /* Fan speed in RPM */
   int fan_load;               /* Fan load percentage (0-100) */
   float battery_voltage;      /* Bus voltage in volts */
   float battery_current;      /* Current in amps */
   float battery_consumption;  /* Power in watts */
   float battery_temperature;  /* Temperature of INA238 die in °C */
   float battery_level;        /* Battery level percentage (0-100) */
   char battery_status[16];    /* Battery status string (e.g., "NORMAL", "WARNING", "CRITICAL") */
   float time_remaining_min;   /* Estimated battery runtime remaining in minutes */
   char time_remaining_fmt[8]; /* Formatted battery runtime remaining (HH:MM) */
   char battery_chemistry[8];  /* Battery chemistry type (e.g., "LiPo", "Li-Ion") */
   float battery_capacity_mah; /* Battery capacity in milliamp-hours */
   int battery_cells;          /* Number of battery cells */
   
   /* Timestamp of last update for each metric */
   time_t cpu_update_time;
   time_t memory_update_time;
   time_t fan_update_time;
   time_t power_update_time;
   
   /* Status flags for each metric (true if valid/available) */
   bool cpu_available;
   bool memory_available;
   bool fan_available;
   bool power_available;
} system_metrics_t;

/* Declaration of global metrics structure */
extern system_metrics_t system_metrics;

/* Function declarations */
void init_system_metrics(void);
bool is_metric_stale(time_t update_time, int timeout_seconds);
void update_metrics_availability(int timeout_seconds);
float get_cpu_usage(void);
float get_memory_usage(void);
int get_fan_rpm(void);
int get_fan_load_percent(void);

#endif /* SYSTEM_METRICS_H */

