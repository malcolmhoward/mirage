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

#ifndef RECORDING_H
#define RECORDING_H

#include <glib-2.0/glib.h>
#include <gst/gst.h>
#include "defines.h"
#include "mirage.h"

/* Function to set the recording state */
void set_recording_state(DestinationType state);

/* Function to get the recording state */
DestinationType get_recording_state(void);

/* Function to get the recording started state */
int get_recording_started(void);

/**
 * Thread function for video processing and encoding
 *
 * @param arg Thread argument (unused)
 * @return NULL on completion
 */
void *video_next_thread(void *arg);

/* GStreamer callback for when the appsrc needs data */
void start_feed(GstElement *source, guint size, void *data);

/* GStreamer callback for when the appsrc has enough data */
void stop_feed(GstElement *source, void *data);

/* Get a pointer to the current video out data structure */
video_out_data *get_video_out_data(void);

/* Get the current video out thread ID */
pthread_t get_video_out_thread(void);

/* Sets the video out thread ID */
void set_video_out_thread(pthread_t thread_id);

/* Reset the video out thread ID to 0 (not running) */
void reset_video_out_thread(void);

/* Set the recording path directory */
void set_video_recording_path(const char *path);

/* Initialize the p_mutex in video_out_data at program start */
void init_video_out_data(void);

/* Cleanup the p_mutex in video_out_data at program exit */
void cleanup_video_out_data(void);
 

#endif /* RECORDING_H */
