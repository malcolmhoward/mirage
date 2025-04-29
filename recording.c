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
#include <sys/time.h>
#include <pthread.h>
#include <sys/types.h>
#include <limits.h>
#include <glib-2.0/glib.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <SDL2/SDL.h>
#include <unistd.h>

#include "config_manager.h"
#include "defines.h"
#include "logging.h"
#include "recording.h"
#include "secrets.h"
#include "utils.h"

/* Static (internal) variables */
static int feed_me = 0;             /* Control the feeding of the encoding thread */
static pthread_t vid_out_thread = 0; /* Thread ID for video output */
static char record_path[PATH_MAX] = "."; /* Path for saving recordings */

/* Global video output data structure */
static video_out_data this_vod = {
   .output = DISABLED,
   .buffer_num = 0,
   .rgb_out_pixels = {NULL, NULL},
   .filename = "",
   .started = 0,
   .outfile = NULL
};

/* Initialize the p_mutex in video_out_data at program start */
void init_video_out_data(void) {
   pthread_mutex_init(&this_vod.p_mutex, NULL);
}

/* Cleanup the p_mutex in video_out_data at program exit */
void cleanup_video_out_data(void) {
   pthread_mutex_destroy(&this_vod.p_mutex);
}

/* Set the recording path directory */
void set_video_recording_path(const char *path) {
   if (path != NULL) {
      strncpy(record_path, path, PATH_MAX - 1);
      record_path[PATH_MAX - 1] = '\0';
   }
}

/* Returns a pointer to the global video out data structure */
video_out_data *get_video_out_data(void) {
   return &this_vod;
}

/* Returns the current video out thread ID */
pthread_t get_video_out_thread(void) {
   return vid_out_thread;
}

/* Sets the video out thread ID */
void set_video_out_thread(pthread_t thread_id) {
   vid_out_thread = thread_id;
}

/* Reset the video out thread ID to 0 (not running) */
void reset_video_out_thread(void) {
   vid_out_thread = 0;
}

/**
 * Sets the recording state and announces the change via MQTT.
 *
 * @param state New recording state to set
 */
void set_recording_state(DestinationType state) {
   char announce[35] = "";

   if (this_vod.output == state) {
      LOG_INFO("Recording state unchanged.");

      return;
   }

   switch (state) {
      case DISABLED:
         snprintf(announce, sizeof(announce), "Stopping recording and streaming.");
         break;
      case RECORD:
         snprintf(announce, sizeof(announce), "Starting recording.");
         break;
      case STREAM:
         snprintf(announce, sizeof(announce), "Starting streaming.");
         break;
      case RECORD_STREAM:
         snprintf(announce, sizeof(announce), "Starting recording and streaming.");
         break;
      default:
         snprintf(announce, sizeof(announce), "Unknown recording state requested.");
         break;
   }

   mqttTextToSpeech(announce);

   this_vod.output = state;
}

/**
 * Gets the recording state.
 *
 * @return DestinationType Recording state.
 */
DestinationType get_recording_state(void) {
   return this_vod.output;
}

/**
 * Gets the recording started state.
 *
 * @return int recording started state.
 */
int get_recording_started(void) {
   return this_vod.started;
}

/**
 * GStreamer callback for when appsrc needs data.
 * Sets the feed_me flag to indicate that the pipeline is ready for more data.
 *
 * @param source GStreamer element (appsrc)
 * @param size Requested size (unused)
 * @param data User data (unused)
 */
void start_feed(GstElement *source, guint size, void *data) {
   feed_me = 1;
}

/**
 * GStreamer callback for when appsrc has enough data.
 * Clears the feed_me flag to indicate that no more data is needed right now.
 *
 * @param source GStreamer element (appsrc)
 * @param data User data (unused)
 */
void stop_feed(GstElement *source, void *data) {
   feed_me = 0;
}

/**
 * Thread function for video processing and encoding.
 * Handles capturing frames from SDL, encoding them, and saving/streaming as requested.
 *
 * @param arg Thread argument (unused)
 * @return NULL on completion
 */
void *video_next_thread(void *arg) {
   time_t r_time;
   struct tm *l_time = NULL;
   char datetime[16];

   gchar descr[GSTREAMER_PIPELINE_LENGTH];
   GstElement *pipeline = NULL, *srcEncode = NULL;
   GError *error = NULL;

   GstFlowReturn ret = -1;
   GstBuffer *buffer = NULL;
   GstCaps *caps = NULL;

   /* A couple of the variables below were being optimized out even though they were setting
    * used parameters. This "volatile" trick to prevent them from being optimized out is a new
    * one for me but it works. */
   GstClock *pipeline_clock = NULL;
   volatile GstClockTime running_time;
   volatile GstClockTime base_time;
   volatile guint64 count = 0;
   volatile GstClockTime current_time;

   struct timespec start_time, end_time;
   long processing_time_us = 0L, delay_time_us = 0L;

   int local_window_width = 0, local_window_height = 0;

   GstBus *bus = NULL;

   stream_settings *this_ss = get_stream_settings();

   /* Get window dimensions from renderer */
   SDL_Renderer *renderer = get_sdl_renderer();
   SDL_GetRendererOutputSize(renderer, &local_window_width, &local_window_height);

   /* We date code our recordings */
   time(&r_time);
   l_time = localtime(&r_time);
   strftime(datetime, sizeof(datetime), "%Y%m%d_%H%M%S", l_time);

#ifdef MKV_OUT
   snprintf(this_vod.filename, sizeof(this_vod.filename), "%s/ironman-vid-%s.mkv", record_path, datetime);
#else
   snprintf(this_vod.filename, sizeof(this_vod.filename), "%s/ironman-vid-%s.mp4", record_path, datetime);
#endif

   if (this_vod.output == RECORD_STREAM) {
      LOG_INFO("New recording: %s", this_vod.filename);
      g_snprintf(descr, GSTREAMER_PIPELINE_LENGTH, GST_ENCSTR_PIPELINE, local_window_width, local_window_height,
                 TARGET_RECORDING_FPS, this_vod.filename,
                 this_ss->stream_width, this_ss->stream_height, this_ss->stream_dest_ip);
   } else if (this_vod.output == RECORD) {
      LOG_INFO("New recording: %s", this_vod.filename);
#ifdef RECORD_AUDIO
      g_snprintf(descr, GSTREAMER_PIPELINE_LENGTH, GST_ENC_PIPELINE, local_window_width, local_window_height,
                 TARGET_RECORDING_FPS, RECORD_PULSE_AUDIO_DEVICE, this_vod.filename);
#else
      g_snprintf(descr, GSTREAMER_PIPELINE_LENGTH, GST_ENC_PIPELINE, local_window_width, local_window_height,
                 TARGET_RECORDING_FPS, this_vod.filename);
#endif
      LOG_INFO("desc: %s", descr);
   } else if (this_vod.output == STREAM) {
#ifdef RECORD_AUDIO
      g_snprintf(descr, GSTREAMER_PIPELINE_LENGTH, GST_STR_PIPELINE,
                 local_window_width, local_window_height, TARGET_RECORDING_FPS,
                 STREAM_WIDTH, STREAM_HEIGHT, STREAM_BITRATE,
                 RECORD_PULSE_AUDIO_DEVICE,
                 YOUTUBE_STREAM_KEY);
#else
      g_snprintf(descr, GSTREAMER_PIPELINE_LENGTH, GST_STR_PIPELINE,
                 local_window_width, local_window_height, TARGET_RECORDING_FPS,
                 STREAM_WIDTH, STREAM_HEIGHT, STREAM_BITRATE,
                 YOUTUBE_STREAM_KEY);
#endif
   } else {
      LOG_ERROR("Invalid destination passed.");
      return NULL;
   }

   pipeline = gst_parse_launch(descr, &error);
   if (error != NULL) {
      SDL_Log("could not construct pipeline \"%s\": %s\n", descr, error->message);
      g_error_free(error);
      this_vod.output = 0;
      return NULL;
   }

   bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

   /* Get sink */
   srcEncode = gst_bin_get_by_name(GST_BIN(pipeline), "srcEncode");

   g_signal_connect(srcEncode, "need-data", G_CALLBACK(start_feed), NULL);
   g_signal_connect(srcEncode, "enough-data", G_CALLBACK(stop_feed), NULL);

   /* Set the caps on the source */
   caps = gst_caps_new_simple("video/x-raw",
      "bpp", G_TYPE_INT, 32,
      "depth", G_TYPE_INT, 32,
      "width", G_TYPE_INT, local_window_width,
      "height", G_TYPE_INT, local_window_height,
      NULL);
   gst_app_src_set_caps(GST_APP_SRC(srcEncode), caps);
   gst_caps_unref(caps);

   g_object_set(G_OBJECT(srcEncode),
             "is-live", TRUE,
             NULL);

   gst_element_set_state(pipeline, GST_STATE_PLAYING);
   this_vod.started = 1;

   pipeline_clock = gst_element_get_clock(pipeline);
   if (pipeline_clock == NULL) {
      SDL_Log("Error getting pipeline clock. This output cannot be recorded.\n");
      this_vod.output = 0;
   }

   base_time = gst_element_get_base_time(pipeline);

   while (this_vod.output) {
      if (feed_me) {
         pthread_mutex_lock(&this_vod.p_mutex);

         clock_gettime(CLOCK_MONOTONIC, &start_time);

         if (this_vod.rgb_out_pixels[this_vod.buffer_num] != NULL) {
            buffer = gst_buffer_new_wrapped(this_vod.rgb_out_pixels[this_vod.buffer_num],
                                            local_window_width * RGB_OUT_SIZE * local_window_height);
            if (buffer == NULL) {
               LOG_ERROR("Failure to allocate new buffer for encoding.");
               break;
            }
            current_time = gst_clock_get_time(pipeline_clock);
            running_time = current_time - base_time;

            GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, GST_SECOND, 30);
            GST_BUFFER_OFFSET(buffer)   = count++;
            GST_BUFFER_PTS(buffer)      = running_time;
            GST_BUFFER_DTS(buffer)      = GST_CLOCK_TIME_NONE;

            /* Get the preroll buffer from appsink */
            ret = gst_app_src_push_buffer(GST_APP_SRC(srcEncode), buffer);

            this_vod.rgb_out_pixels[this_vod.buffer_num] = NULL;

            if (ret != GST_FLOW_OK) {
               LOG_ERROR("GST_FLOW error while pushing buffer: %d", ret);
               break;
            }
         }
         clock_gettime(CLOCK_MONOTONIC, &end_time);

         pthread_mutex_unlock(&this_vod.p_mutex);
      }
      
      /* Calculate processing time in microseconds */
      processing_time_us = (end_time.tv_sec - start_time.tv_sec) * 1000000L + (end_time.tv_nsec - start_time.tv_nsec) / 1000L;

      /* Calculate how long to delay to maintain the target frame rate */
      delay_time_us = TARGET_RECORDING_FRAME_DURATION_US - processing_time_us;

      /* Apply the calculated delay, if positive */
      if (delay_time_us > 0) {
         usleep(delay_time_us);
      }
   }
   
   if (pipeline_clock != NULL) {
      gst_object_unref(pipeline_clock);
   }

   /* Video */
   this_vod.started = 0;
   g_signal_emit_by_name(srcEncode, "end-of-stream", &ret);
   gst_element_set_state((GstElement *) pipeline, GST_STATE_NULL);

   gst_object_unref(bus);
   gst_object_unref(pipeline);

   /* Reset the global thread ID to indicate this thread is done */
   vid_out_thread = 0;

   return NULL;
}
