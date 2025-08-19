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
#define NSEC_PER_SEC 1000000000L

/* Rotate triple buffer indices in a circular pattern */
void rotate_triple_buffer_indices(video_out_data *vod) {
   int temp = vod->buffer_num;
   vod->buffer_num = vod->read_index;
   vod->read_index = vod->write_index;
   vod->write_index = temp;
}

/* Global video output data structure */
static video_out_data this_vod = {
   .output = DISABLED,
   .buffer_num = 0,
   .read_index = 2,
   .write_index = 1,
   .pipeline = NULL,
   .rgb_out_pixels = {NULL, NULL, NULL},
   .filename = "",
   .started = 0,
   .outfile = NULL
};

/* Initialize the p_mutex in video_out_data at program start */
void init_video_out_data(void) {
   pthread_mutex_init(&this_vod.p_mutex, NULL);

   /* Initialize triple buffer indices */
   this_vod.buffer_num = 0;
   this_vod.read_index = 2;
   this_vod.write_index = 1;

   /* Initialize buffers to NULL */
   this_vod.rgb_out_pixels[0] = NULL;
   this_vod.rgb_out_pixels[1] = NULL;
   this_vod.rgb_out_pixels[2] = NULL;
}

/* Cleanup the p_mutex in video_out_data at program exit */
void cleanup_video_out_data(void) {
   /* Free all buffers */
   for (int i = 0; i < 3; i++) {
      if (this_vod.rgb_out_pixels[i] != NULL) {
         free(this_vod.rgb_out_pixels[i]);
         this_vod.rgb_out_pixels[i] = NULL;
      }
   }

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
 * Clean up GStreamer pipeline resources.
 *
 * @param pipeline The main GStreamer pipeline
 * @param srcEncode The source element used for sending data
 * @param bus The bus associated with the pipeline (can be NULL if already unref'd)
 */
void cleanup_pipeline(GstElement *pipeline, GstElement *srcEncode, GstBus *bus) {
   video_out_data *vod = get_video_out_data();

   if (srcEncode) {
      // Disconnect signal handlers if they were connected
      gulong need_data_id = g_signal_handler_find(srcEncode, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                               G_CALLBACK(start_feed), NULL);
      if (need_data_id > 0) {
         g_signal_handler_disconnect(srcEncode, need_data_id);
      }

      gulong enough_data_id = g_signal_handler_find(srcEncode, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                                 G_CALLBACK(stop_feed), NULL);
      if (enough_data_id > 0) {
         g_signal_handler_disconnect(srcEncode, enough_data_id);
      }

      gst_object_unref(srcEncode);
   }

   if (pipeline) {
      gst_element_set_state(pipeline, GST_STATE_NULL);
      // Wait for state change to complete
      GstState state;
      gst_element_get_state(pipeline, &state, NULL, 100 * GST_MSECOND);
      gst_object_unref(pipeline);
      vod->pipeline = NULL;
   }

   LOG_INFO("Pipeline resources cleaned up");
}

/**
 * Sets the recording/streaming state of the application.
 */
void set_recording_state(DestinationType state)
{
   video_out_data *this_vod = get_video_out_data();

   if (state == DISABLED && this_vod->output != DISABLED) {
      LOG_INFO("Stopping recording/streaming...");

      /* Signal thread to stop */
      this_vod->output = DISABLED;

      /* Give thread time to exit cleanly */
      pthread_t thread = get_video_out_thread();
      if (thread != 0) {
         struct timespec timeout;
         clock_gettime(CLOCK_REALTIME, &timeout);
         timeout.tv_sec += 5;  /* 5 second timeout */

         int result = pthread_timedjoin_np(thread, NULL, &timeout);
         if (result == ETIMEDOUT) {
            LOG_ERROR("Thread didn't exit cleanly, forcing termination");
            pthread_cancel(thread);
            pthread_join(thread, NULL);
         }

         set_video_out_thread(0);
      }

      /* Clear buffers */
      pthread_mutex_lock(&this_vod->p_mutex);
      for (int i = 0; i < 3; i++) {
         if (this_vod->rgb_out_pixels[i] != NULL) {
            free(this_vod->rgb_out_pixels[i]);
            this_vod->rgb_out_pixels[i] = NULL;
         }
      }
      pthread_mutex_unlock(&this_vod->p_mutex);
   } else {
      this_vod->output = state;
   }
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

static gboolean bus_message_handler(GstBus *bus, GstMessage *message, gpointer data) {
   video_out_data *vod = (video_out_data *)data;

   switch (GST_MESSAGE_TYPE(message)) {
      case GST_MESSAGE_ERROR: {
         GError *err = NULL;
         gchar *debug_info = NULL;

         gst_message_parse_error(message, &err, &debug_info);
         LOG_ERROR("GStreamer error from %s: %s", GST_OBJECT_NAME(message->src), err->message);
         LOG_ERROR("Debug info: %s", debug_info ? debug_info : "none");

         g_error_free(err);
         g_free(debug_info);

         // Signal main thread to stop recording
         vod->output = DISABLED;
         break;
      }
      case GST_MESSAGE_WARNING: {
         GError *err = NULL;
         gchar *debug_info = NULL;

         gst_message_parse_warning(message, &err, &debug_info);
         LOG_WARNING("GStreamer warning from %s: %s", GST_OBJECT_NAME(message->src), err->message);
         LOG_WARNING("Debug info: %s", debug_info ? debug_info : "none");

         g_error_free(err);
         g_free(debug_info);
         break;
      }
      case GST_MESSAGE_STATE_CHANGED: {
         // Log state changes for pipeline to help with debugging
         if (GST_MESSAGE_SRC(message) == GST_OBJECT(vod->pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
            LOG_INFO("Pipeline state changed from %s to %s, pending: %s",
                    gst_element_state_get_name(old_state),
                    gst_element_state_get_name(new_state),
                    gst_element_state_get_name(pending_state));
         }
         break;
      }
      case GST_MESSAGE_EOS:
         LOG_INFO("End of stream received");
         break;
      case GST_MESSAGE_QOS:
         /* These are very frequent, so only log at debug level or ignore */
         break;
      case GST_MESSAGE_ASYNC_DONE:
         /* Pipeline is now ready for data flow */
         LOG_INFO("Pipeline is ready for data flow");
         break;
      default:
         /* Log the message type name for debugging */
         LOG_INFO("Unhandled GStreamer message type: %s from %s",
                GST_MESSAGE_TYPE_NAME(message),
                GST_OBJECT_NAME(GST_MESSAGE_SRC(message)));
         break;
      break;
   }

   return TRUE;
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
   GstBus *bus = NULL;
   GstStateChangeReturn state_ret;
   gulong need_data_signal_id = 0, enough_data_signal_id = 0;

   GstFlowReturn ret = -1;
   GstBuffer *buffer = NULL;
   GstCaps *caps = NULL;

   /* A couple of the variables below were being optimized out even though they were setting
    * used parameters. This "volatile" trick to prevent them from being optimized out is a new
    * one for me but it works. */
   GstClock *pipeline_clock = NULL;
   volatile GstClockTime base_time;
   volatile guint64 count = 0;

   struct timespec start_time, end_time;
   struct timespec ts_delay;
   long processing_time_ns = 0L, delay_time_ns = 0L;

   int window_width = 0, window_height = 0;

   time_t last_successful_push = time(NULL);
   int frames_pushed = 0;

   /* Get window dimensions */
   get_window_size(&window_width, &window_height);

   /* We date code our recordings */
   time(&r_time);
   l_time = localtime(&r_time);
   strftime(datetime, sizeof(datetime), "%Y%m%d_%H%M%S", l_time);

#ifdef MKV_OUT
   snprintf(this_vod.filename, sizeof(this_vod.filename), "%s/ironman-vid-%s.mkv", record_path, datetime);
#else
   snprintf(this_vod.filename, sizeof(this_vod.filename), "%s/ironman-vid-%s.mp4", record_path, datetime);
#endif

   /* Build pipeline description based on output type */
   if (this_vod.output == RECORD_STREAM) {
      LOG_INFO("New recording: %s", this_vod.filename);
      g_snprintf(descr, GSTREAMER_PIPELINE_LENGTH, GST_ENCSTR_PIPELINE,
                 window_width, window_height, TARGET_RECORDING_FPS,
                 STREAM_WIDTH, STREAM_HEIGHT, STREAM_BITRATE,
                 RECORD_PULSE_AUDIO_DEVICE,
                 this_vod.filename,
                 YOUTUBE_STREAM_KEY);
   } else if (this_vod.output == RECORD) {
      LOG_INFO("New recording: %s", this_vod.filename);
      g_snprintf(descr, GSTREAMER_PIPELINE_LENGTH, GST_ENC_PIPELINE, window_width, window_height,
                 TARGET_RECORDING_FPS, RECORD_PULSE_AUDIO_DEVICE, this_vod.filename);
      LOG_INFO("descr: %s", descr);
   } else if (this_vod.output == STREAM) {
      g_snprintf(descr, GSTREAMER_PIPELINE_LENGTH, GST_STR_PIPELINE,
                 window_width, window_height, TARGET_RECORDING_FPS,
                 STREAM_WIDTH, STREAM_HEIGHT, STREAM_BITRATE,
                 RECORD_PULSE_AUDIO_DEVICE,
                 YOUTUBE_STREAM_KEY);
   } else {
      LOG_ERROR("Invalid destination passed.");
      this_vod.output = DISABLED;
      return NULL;
   }

   // Simple validation to check for obvious issues
   if (strlen(descr) == 0 || strlen(descr) >= GSTREAMER_PIPELINE_LENGTH - 1) {
      LOG_ERROR("Invalid pipeline description length: %zu", strlen(descr));
      this_vod.output = DISABLED;
      return NULL;
   }

   // Log pipeline string for debugging
   printf("Creating pipeline: %s", descr);

   pipeline = gst_parse_launch(descr, &error);
   if (error != NULL) {
      LOG_ERROR("Failed to create pipeline: %s", error->message);
      g_error_free(error);
      this_vod.output = DISABLED;
      return NULL;
   }

   this_vod.pipeline = pipeline;

   // Enable latency adjustment for better AV sync
   gst_pipeline_set_auto_flush_bus(GST_PIPELINE(pipeline), TRUE);
   gst_pipeline_set_latency(GST_PIPELINE(pipeline), 100 * GST_MSECOND); // Adjust latency as needed

   // Add clock management for better AV sync
   GstClock *system_clock = gst_system_clock_obtain();
   if (system_clock != NULL) {
      LOG_INFO("Setting pipeline to use system clock");
      gst_pipeline_use_clock(GST_PIPELINE(pipeline), system_clock);
      // Don't unref the clock here - pipeline will use it
   } else {
      LOG_WARNING("Failed to obtain system clock");
   }

   bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
   guint bus_watch_id = gst_bus_add_watch(bus, bus_message_handler, &this_vod);
   gst_object_unref(bus);

   /* Get sink */
   srcEncode = gst_bin_get_by_name(GST_BIN(pipeline), "srcEncode");
   if (!srcEncode) {
      LOG_ERROR("Failed to find 'srcEncode' element in the pipeline.");
      cleanup_pipeline(pipeline, srcEncode, bus);
      return NULL;
   }

   need_data_signal_id = g_signal_connect(srcEncode, "need-data", G_CALLBACK(start_feed), NULL);
   enough_data_signal_id = g_signal_connect(srcEncode, "enough-data", G_CALLBACK(stop_feed), NULL);

   if (need_data_signal_id == 0 || enough_data_signal_id == 0) {
      LOG_ERROR("Failed to connect signal handlers.");
      cleanup_pipeline(pipeline, srcEncode, bus);
      return NULL;
   }

   /* Set the caps on the source */
   caps = gst_caps_new_simple("video/x-raw",
      "bpp", G_TYPE_INT, 32,
      "depth", G_TYPE_INT, 32,
      "width", G_TYPE_INT, window_width,
      "height", G_TYPE_INT, window_height,
      NULL);

   gst_app_src_set_caps(GST_APP_SRC(srcEncode), caps);
   gst_caps_unref(caps);

   // Configure appsrc properties
   g_object_set(G_OBJECT(srcEncode),
      // Basic properties
      "is-live", TRUE,                  // Mark as a live source
      "format", GST_FORMAT_TIME,        // Use time format for buffers
      "do-timestamp", TRUE,             // Add timestamps to buffers

      // Stream configuration
      "stream-type", GST_APP_STREAM_TYPE_STREAM, // Continuous stream of buffers

      // Flow control
      "emit-signals", TRUE,             // Emit signals for flow control

      NULL);

   state_ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
   if (state_ret == GST_STATE_CHANGE_FAILURE) {
      LOG_ERROR("Failed to set pipeline to PLAYING state");
      cleanup_pipeline(pipeline, srcEncode, bus);
      return NULL;
   }
   else if (state_ret == GST_STATE_CHANGE_ASYNC) {
      LOG_INFO("Pipeline state change is ASYNC - waiting for data...");
   }

   pipeline_clock = gst_element_get_clock(pipeline);
   if (pipeline_clock == NULL) {
      LOG_ERROR("Failed to get pipeline clock");
      cleanup_pipeline(pipeline, srcEncode, bus);
      return NULL;
   }

   this_vod.started = 1;
   base_time = gst_element_get_base_time(pipeline);
   LOG_INFO("Pipeline successfully started");

   while (this_vod.output) {
      if (feed_me) {
         clock_gettime(CLOCK_MONOTONIC, &start_time);

         pthread_mutex_lock(&this_vod.p_mutex);

         /* Use buffer_num - that's what your triple buffer system provides */
         if (this_vod.rgb_out_pixels[this_vod.buffer_num] != NULL) {
            /* CRITICAL FIX: Make a copy for GStreamer so no ownership conflict */
            size_t buffer_size = window_width * RGB_OUT_SIZE * window_height;
            void *buffer_copy = malloc(buffer_size);

            if (buffer_copy) {
               memcpy(buffer_copy, this_vod.rgb_out_pixels[this_vod.buffer_num], buffer_size);

               /* Give the copy to GStreamer - it will free it */
               buffer = gst_buffer_new_wrapped(buffer_copy, buffer_size);

               /* Set timestamps */
               GstClockTime pts = gst_clock_get_time(pipeline_clock) - base_time;

               GST_BUFFER_PTS(buffer) = pts;
               GST_BUFFER_DTS(buffer) = pts;  /* Set DTS same as PTS */
               GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, GST_SECOND, TARGET_RECORDING_FPS);
               GST_BUFFER_OFFSET(buffer) = count++;

               /* Push buffer */
               ret = gst_app_src_push_buffer(GST_APP_SRC(srcEncode), buffer);

               if (ret != GST_FLOW_OK) {
                  LOG_ERROR("GST_FLOW error while pushing buffer: %d", ret);
                  pthread_mutex_unlock(&this_vod.p_mutex);
                  break;
               }

               frames_pushed++;
               last_successful_push = time(NULL);
            }
         }

         pthread_mutex_unlock(&this_vod.p_mutex);
      }

      /* Keep your existing timing code */
      clock_gettime(CLOCK_MONOTONIC, &end_time);
      processing_time_ns = (end_time.tv_sec - start_time.tv_sec) * NSEC_PER_SEC +
                          (end_time.tv_nsec - start_time.tv_nsec);
      delay_time_ns = TARGET_RECORDING_FRAME_DURATION_US * 1000L - processing_time_ns;

      if (delay_time_ns > 0) {
         ts_delay.tv_sec = delay_time_ns / NSEC_PER_SEC;
         ts_delay.tv_nsec = delay_time_ns % NSEC_PER_SEC;
         clock_nanosleep(CLOCK_MONOTONIC, 0, &ts_delay, NULL);
      }

      if (time(NULL) - last_successful_push > 30) {
         LOG_ERROR("Stream frozen - attempting restart...");

         /* Clean shutdown */
         if (srcEncode) {
            gst_app_src_end_of_stream(GST_APP_SRC(srcEncode));
         }
         gst_element_set_state(pipeline, GST_STATE_NULL);

         /* Clear and restart */
         DestinationType orig_output = this_vod.output;
         this_vod.output = DISABLED;
         sleep(2);

         /* Signal main thread to restart */
         set_recording_state(orig_output);

         break;  /* Exit this thread, new one will be created */
      }
   }

   LOG_INFO("Shutting down pipeline");
   this_vod.started = 0;
   
   // Send EOS and wait for it to propagate
   LOG_INFO("Sending EOS to pipeline");
   if (srcEncode) {
      g_signal_emit_by_name(srcEncode, "end-of-stream", &ret);
   }

   if (pipeline_clock != NULL) {
      gst_object_unref(pipeline_clock);
   }

   if (bus_watch_id > 0) {
      g_source_remove(bus_watch_id);
   }

   cleanup_pipeline(pipeline, srcEncode, bus);

   LOG_INFO("Pipeline shutdown complete");

   reset_video_out_thread();

   return NULL;
}

