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
 *
 * String Utilities - Safe string functions for embedded use
 *
 * Ported from DAWN common/include/utils/string_utils.h to provide
 * consistent safe string handling across OASIS projects.
 */

#ifndef MIRAGE_STRING_UTILS_H
#define MIRAGE_STRING_UTILS_H

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Safe string copy with guaranteed null-termination
 *
 * Unlike strncpy, this always null-terminates the destination buffer
 * and doesn't waste cycles padding with zeros. This is a portable
 * replacement for strlcpy which isn't available on all platforms.
 *
 * Thread Safety: This function is thread-safe (modifies only dest buffer).
 *
 * @param dest Destination buffer
 * @param src Source string (must be null-terminated)
 * @param size Size of destination buffer
 */
static inline void safe_strncpy(char *dest, const char *src, size_t size) {
   if (size == 0) {
      return;
   }
   size_t len = strlen(src);
   if (len >= size) {
      len = size - 1;
   }
   memcpy(dest, src, len);
   dest[len] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif /* MIRAGE_STRING_UTILS_H */
