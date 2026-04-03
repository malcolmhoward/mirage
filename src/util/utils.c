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
#include <sys/stat.h>
#include <unistd.h>

/* Check to see if the file has grown in size. Save new size to passed in value. */
int has_file_grown(const char *filename, off_t *last_size) {
   struct stat sb;

   if (stat(filename, &sb) == -1) {
      perror("stat");
      return -1;
   }

   if (sb.st_size > *last_size) {
      *last_size = sb.st_size;
      return 0;
   }

   return 1;
}
