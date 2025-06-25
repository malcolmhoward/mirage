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

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

#include "config_manager.h"
#include "devices.h"
#include "logging.h"
#include "mirage.h"
#include "system_metrics.h"

long double get_loadavg(void)
{
   float cpu_usage = get_cpu_usage();
   return (cpu_usage >= 0.0f) ? (long double)cpu_usage : 0.0L;
}

long double get_mem_usage(void)
{
   float memory_usage = get_memory_usage();
   return (memory_usage >= 0.0f) ? (long double)memory_usage : 0.0L;
}

/* Get the wifi signal level from the wireless driver.
 * This returns 0-9 for display purposes. */
int get_wifi_signal_level(void)
{
   FILE *fp = NULL;
   char buf[125];
   char *found = NULL;
   char s_signal[7];
   int signal = -1;
   int level = 0;               /* 0-9 based on -30 to -90 dBm) */

   fp = fopen("/proc/net/dev", "r");
   if (fp == NULL) {
      LOG_ERROR("No wireless found.");
      return 0;
   }

   while (fgets(buf, 125, fp) != NULL) {
      if ((found = strstr(buf, get_wifi_dev_name())) != NULL) {
         break;
      }
   }

   fclose(fp);

   if (found != NULL) {
      memset((void *)s_signal, '\0', 7);
      strncpy(s_signal, &found[19], 6);
      signal = atoi(s_signal);

      // Map from 0-9. Arduino map equation.
      if (signal == 0) {
         level = 0;
      } else if (signal > 0) {
         level = round((double)signal / 10.0);
      } else {
         level = (signal - -90) * (9 - 0) / (-30 - -90) + 0;
      }
   }
   //printf("Wireless signal: %d, level :%d\n", signal, level);
   if (level > 9)
      level = 9;
   if (level < 0)
      level = 0;

   return level;
}


