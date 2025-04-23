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

/* Threading Information */
/* vid_out_thread    - Video output processing. Disk and/or streaming.
 * thread_handles[#] - NUM_AUDIO_THREADS number of audio threads.
 * video_proc_thread - Video input processing. Cameras to SDL.
 * command_proc_thread - USB/Serial input handling.
 * od_[LR]_thread    - If object detection is enabled. These will handle
 *                     object detection for each eye.
 * cpu_util_thread   - If a CPU utilization UI element is enabled, this
 *                     will keep that up-to-date.
 * map_download_thread - If a map UI element is enabled, this will keep
 *                       that up-to-date.
 * mosquitto loop    - There is a background process monitoring MQTT.
 *                     The thread is defined externally with callbacks
 *                     here.
 */

#define _GNU_SOURCE
/* Std C */
#include <ctype.h>
#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Serial Port */
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>

/* Sockets */
#include <netdb.h>
#include <netinet/in.h>

/* Vorbis */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "vorbis/codec.h"
#include "vorbisfile.h"
#pragma GCC diagnostic pop

/* POSIX Message Queue */
#include <fcntl.h>
#include <mqueue.h>
#include <sys/stat.h>

/* SDL2 */
#include "SDL2/SDL.h"
#include "SDL2/SDL_image.h"
#include "SDL2/SDL_ttf.h"

/* JSON */
#include <json-c/json.h>

/* CURL */
#include <curl/curl.h>

/* GStreamer */
#include <glib-2.0/glib.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

/* Mosquitto */
#include <mosquitto.h>

/* NVIDIA/CUDA */
#ifdef USE_CUDA
#include <cuda_runtime.h>
//#include <nvbuf_utils.h>
#endif

#include <GL/glew.h>
#include <stdbool.h>

/* Local */
#include "defines.h" /* Out of order due to dependencies */
#include "audio.h"
#include "armor.h"
#include "command_processing.h"
#include "config_manager.h"
#include "config_parser.h"
#include "curl_download.h"
#include "devices.h"
#include "element_renderer.h"
#include "frame_rate_tracker.h"
#include "hud_manager.h"
#include "image_utils.h"
#include "logging.h"
#include "mirage.h"
#include "mosquitto_comms.h"
#include "secrets.h"
#include "utils.h"
#include "version.h"

pthread_mutex_t v_mutex = PTHREAD_MUTEX_INITIALIZER;  /* Mutex for video buffer access. */
GstMapInfo mapL[2], mapR[2];        /* Video memory maps. */
#ifdef DISPLAY_TIMING
struct timespec ts_cap[2];          /* Store the display latency. */
#endif
int video_posted = 0;               /* Notify that the new video frames are ready. */
int buffer_num = 0;                 /* Video is double buffered. This swaps between them. */

static char record_path[PATH_MAX];  /* Where do we store recordings? */

static int window_width = 0;
static int window_height = 0;
static pthread_mutex_t windowSizeMutex = PTHREAD_MUTEX_INITIALIZER;

/* Video Buffers */
video_out_data this_vod;

pthread_t vid_out_thread = 0;                /* Video output thread ID */
pthread_t od_L_thread = 0, od_R_thread = 0;  /* Object detection thread IDs */
static int single_cam = 0;                   /* Single Camera Mode Enable */
static int cam1_id = -1, cam2_id = -1;       /* Camera IDs for CSI or USB */

static struct mosquitto *mosq = NULL;        /* MQTT pointer */

static int quit = 0;                         /* Global to sync exiting of threads */
int detect_enabled = 0;                      /* Is object detection enabled? */

static int snapshot = 0;                     /* Take snapshot flag */
static char snapshot_filename[PATH_MAX+29];  /* Filename to store the snapshot */

double averageFrameRate = 0.0;

/* Right now we only support one instance of each. These are their objects. */
motion this_motion;
enviro this_enviro;
gps this_gps = {
   .time = "00:00:00",
   .date = "2021/01/01",
   .fix = 0,
   .quality = 0,
   .latitude = 0.0,
   .latitudeDegrees = 0.0,
   .lat = "0N",
   .longitude = 0.0,
   .longitudeDegrees = 0.0,
   .lon = "0W",
   .speed = 0.0,
   .angle = 0.0,
   .altitude = 0.0,
   .satellites = 0
};
#define AI_NAME_MAX_LENGTH 32
#define AI_STATE_MAX_LENGTH 18   /* This is actually defined by the states in DAWN. */
char aiName[AI_NAME_MAX_LENGTH] = "";
char aiState[AI_STATE_MAX_LENGTH] = "";

/* Audio */
extern thread_info audio_threads[NUM_AUDIO_THREADS];
mqd_t qd_server;
extern mqd_t qd_clients[NUM_AUDIO_THREADS];

const struct Alert alert_messages[ALERT_MAX] = {
   {ALERT_RECORDING, "ERROR: Recording failed!"}
};

alert_t active_alerts = ALERT_NONE;

element default_element =
{
   .type = STATIC,
   .enabled = 0,

   .name = "",
   .hotkey = "",

   .filename = "",
   .filename_r = "",
   .filename_s = "",
   .filename_rs = "",

   .filename_online = "",
   .filename_warning = "",
   .filename_offline = "",

   .src_dst_rect = {0, 0, 0, 0},
   .dst_dst_rect = {0, 0, 0, 0},
   .anim_dur = 0,
   .anim_frames_calc = 0,
   .dyn_anim_frame = 0,

   .text = "",
   .font = "",
   //SDL_Color font_color;
   .ttf_font = NULL,
   .font_size = -1,
   .halign = "left",

   .dest_x = 0,
   .dest_y = 0,
   .angle = 0.0,
   .fixed = 0,

   .layer = 0,

   .surface = NULL,
   .texture = NULL,
   .texture_r = NULL,
   .texture_s = NULL,
   .texture_rs = NULL,
   .texture_l = NULL,
   .texture_w = NULL,
   .texture_p = NULL,

   .texture_base = NULL,
   .texture_online = NULL,
   .texture_warning = NULL,
   .texture_offline = NULL,

   .dst_rect = {0, 0, 0, 0},

   .special_name = "",
   .mqtt_device = "",
   .mqtt_registered = 0,
   .mqtt_last_time = 0,

   .width = 0,
   .height = 0,

   .center_x_offset = 0,
   .center_y_offset = 0,

   .text_x_offset = 0,
   .text_y_offset = 0,

   .this_anim.first_frame = NULL,
   .this_anim.frame_count = 0,

   .warning_temp = -1.0,
   .warning_voltage = -1.0,

   .last_temp = -1.0,
   .last_voltage = -1.0,

   .warn_state = WARN_NORMAL,

   .prev = NULL,
   .next = NULL
};

element *first_element = NULL;               /* Pointer to first UI element. */
element intro_element;                       /* Special intro element. */

/* Local font elements to store each loaded font. */
typedef struct _local_fonts {
   TTF_Font *ttf_font;
   char font[MAX_FILENAME_LENGTH * 2];
   int font_size;

   struct _local_fonts *next;
} local_font;
local_font *font_list = NULL;

/* Detected Objects */
detect this_detect[2][MAX_DETECT];
detect this_detect_sorted[2][MAX_DETECT];

static SDL_Renderer *renderer = NULL;               /* Global SDL Renderer */

od_data oddataL, oddataR;

/* Function Prototypes */
void *video_next_thread(void *arg);

motion *get_motion_dev(void)
{
   return &this_motion;
}

enviro *get_enviro_dev(void)
{
   return &this_enviro;
}

gps *get_gps_dev(void)
{
   return &this_gps;
}

element *get_intro_element(void) {
   return &intro_element;
}

element *get_default_element(void) {
   return &default_element;
}

element *get_first_element(void) {
   return first_element;
}

element *set_first_element(element *this_element)
{
   first_element = this_element;

   return first_element;
}

SDL_Renderer *get_sdl_renderer(void)
{
   return renderer;
}

int set_detect_enabled(int enable)
{
   detect_enabled = enable;

   return detect_enabled;
}

int checkShutdown(void)
{
   return quit;
}

/**
 * Sets a flag to indicate a snapshot event and saves the triggering datetime.
 *
 * This function marks a snapshot event by setting a global flag and saves the provided datetime
 * into a global buffer in a filename.
 *
 * @param datetime The datetime string when the snapshot is triggered, expected to be in the format
 *                 "%Y%m%d_%H%M%S".
 *
 * FIXME: Add mutex protections in case of multiple calls. May even need queuing.
 *
 * Globals:
 * - `snapshot`: An integer flag indicating a snapshot event.
 * - `snapshot_filename`: A buffer storing the filename with datetime of the snapshot event.
 */
void trigger_snapshot(const char *datetime)
{
   snapshot = 1;
   snprintf(snapshot_filename, sizeof(snapshot_filename), "%s/snapshot-%s.jpg",
            record_path, datetime);
}

void process_ai_state(const char *newAIName, const char *newAIState) {
   snprintf(aiName, AI_NAME_MAX_LENGTH, "%s", newAIName);
   snprintf(aiState, AI_STATE_MAX_LENGTH, "%s", newAIState);
}

void set_recording_state(DestinationType state)
{
   char announce[35] = "";

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

/* Free the UI element list. */
void free_elements(element *start_element)
{
   element *this_element = NULL;
   int i = 0;

   if (start_element != NULL) {
      this_element = start_element;
   } else {
      LOG_ERROR("Unable to free NULL elements!");
      return;
   }

   while (this_element != NULL) {
#ifdef DEBUG_SHUTDOWN
      LOG_INFO("Freeing: %d.", this_element->type);
#endif

      if (this_element->surface != NULL) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing surface.");
#endif
         SDL_FreeSurface(this_element->surface);
      }

      if (this_element->texture != NULL) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing texture.");
#endif
         SDL_DestroyTexture(this_element->texture);
      }

      if (this_element->texture_r != NULL) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing texture (r).");
#endif
         SDL_DestroyTexture(this_element->texture_r);
      }

      if (this_element->texture_s != NULL) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing texture (s).");
#endif
         SDL_DestroyTexture(this_element->texture_s);
      }

      if (this_element->texture_rs != NULL) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing texture (rs).");
#endif
         SDL_DestroyTexture(this_element->texture_rs);
      }

      if (this_element->texture_l != NULL) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing texture (l).");
#endif
         SDL_DestroyTexture(this_element->texture_l);
      }

      if (this_element->texture_w != NULL) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing texture (w).");
#endif
         SDL_DestroyTexture(this_element->texture_w);
      }

      if (this_element->texture_p != NULL) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing texture (p).");
#endif
         SDL_DestroyTexture(this_element->texture_p);
      }

      if (this_element->texture_base != NULL) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing texture (base).");
#endif
         SDL_DestroyTexture(this_element->texture_base);
      }

      if (this_element->texture_online != NULL) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing texture (online).");
#endif
         SDL_DestroyTexture(this_element->texture_online);
      }

      if (this_element->texture_warning != NULL) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing texture (warning).");
#endif
         SDL_DestroyTexture(this_element->texture_warning);
      }

      if (this_element->texture_offline != NULL) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing texture (offline).");
#endif
         SDL_DestroyTexture(this_element->texture_offline);
      }

      for (i = 0; i < this_element->this_anim.frame_count; i++) {
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing frame %d.", i);
#endif
         free(this_element->this_anim.frame_lookup[i]);
      }

      if (this_element->next != NULL) {
         this_element = this_element->next;
#ifdef DEBUG_SHUTDOWN
         LOG_INFO("Freeing element.");
#endif
         free(this_element->prev);
      } else {
         free(this_element);
         this_element = NULL;
      }
   }
}

/* Debug function to check proper processing of UI elements. */
void dump_element_list(void)
{
   element *curr_element = first_element;
   int count = 0;

   while (curr_element != NULL) {
      switch (curr_element->type) {
      case STATIC:
         LOG_INFO("Element[%d]:\n"
                "\ttype:\tSTATIC\n"
                "\tfile:\t%s\n"
                "\tdest_x:\t%d\n"
                "\tdest_y:\t%d\n"
                "\tangle:\t%f\n"
                "\tlayer:\t%d",
                count,
                curr_element->filename, curr_element->dest_x, curr_element->dest_y,
                curr_element->angle, curr_element->layer);
         break;
      case ANIMATED:
         LOG_INFO("Element[%d]:\n"
                "\ttype:\tANIMATED\n"
                "\tfile:\t%s\n"
                "\tdest_x:\t%d\n"
                "\tdest_y:\t%d\n"
                "\tangle:\t%f\n"
                "\tlayer:\t%d",
                count,
                curr_element->filename, curr_element->dest_x, curr_element->dest_y,
                curr_element->angle, curr_element->layer);
         break;
      case TEXT:
         LOG_INFO("Element[%d]:\n"
                "\ttype:\tTEXT\n"
                "\tstring:\t%s\n"
                "\tfont:\t%s\n"
                "\tsize:\t%d\n"
                "\tdest_x:\t%d\n"
                "\tdest_y:\t%d\n"
                "\thalign:\t%s\n"
                "\tangle:\t%f\n"
                "\tlayer:\t%d",
                count,
                curr_element->text, curr_element->font, curr_element->font_size,
                curr_element->dest_x, curr_element->dest_y,
                curr_element->halign, curr_element->angle, curr_element->layer);
         break;
      case SPECIAL:
         LOG_INFO("Element[%d]:\n"
                "\ttype:\tSPECIAL\n"
                "\tname:\t%s\n"
                "\tfile:\t%s\n"
                "\tdest_x:\t%d\n"
                "\tdest_y:\t%d\n"
                "\tangle:\t%f\n"
                "\tlayer:\t%d",
                count,
                curr_element->special_name, curr_element->filename,
                curr_element->dest_x, curr_element->dest_y,
                curr_element->angle, curr_element->layer);
         break;
      case ANIMATED_DYNAMIC:
         LOG_INFO("Not implemented.");
         break;
      case ARMOR_COMPONENT:
         LOG_INFO("Not implemented.");
         break;
      }

      count++;
      curr_element = curr_element->next;
   }
}

/* This function plays the intro animation on power up. It is designed to minimize dead loading time.
 * I'd still like to find a better way to do this but due to the way SDL handles threads, it's not
 * that easy. */
int play_intro(int frames, int clear, int *finished)
{
   SDL_Rect src_rect;
   SDL_Rect dst_rect_l, dst_rect_r;
   int frame_count = 0;

   hud_display_settings *this_hds = get_hud_display_settings();

#ifdef ENCODE_TIMING
   int cur_time = 0, max_time = 0, min_time = 0, weight = 0;
   double avg_time = 0.0;
#endif

   if (intro_element.texture == NULL) {
      intro_element.texture = IMG_LoadTexture(renderer, intro_element.this_anim.image);
      if (!intro_element.texture) {
         SDL_Log("Couldn't load %s: %s\n", intro_element.filename, SDL_GetError());
         return 1;
      }
   }

   while (frame_count < frames) {
      frame_count++;

      if (clear) {
         SDL_RenderClear(renderer);
      }

      src_rect.x = intro_element.this_anim.current_frame->source_x;
      src_rect.y = intro_element.this_anim.current_frame->source_y;
      src_rect.w = intro_element.this_anim.current_frame->source_w;
      src_rect.h = intro_element.this_anim.current_frame->source_h;

      dst_rect_l.x = dst_rect_r.x =
          intro_element.dest_x + intro_element.this_anim.current_frame->dest_x;
      dst_rect_l.y = dst_rect_r.y =
          intro_element.dest_y + intro_element.this_anim.current_frame->dest_y;
      dst_rect_l.w = dst_rect_r.w = intro_element.this_anim.current_frame->source_w;
      dst_rect_l.h = dst_rect_r.h = intro_element.this_anim.current_frame->source_h;

      if (intro_element.this_anim.current_frame->next != NULL) {
         if (finished != NULL) {
            *finished = 0;
         }
         intro_element.this_anim.current_frame = intro_element.this_anim.current_frame->next;
      } else {
         if (finished != NULL) {
            *finished = 1;
         }
         intro_element.this_anim.current_frame = intro_element.this_anim.first_frame;
      }

      renderStereo(intro_element.texture, &src_rect, &dst_rect_l, &dst_rect_r, intro_element.angle);

      SDL_RenderPresent(renderer);

      if (this_vod.output) {
#ifdef ENCODE_TIMING
         Uint32 start = 0, stop = 0;
#endif

         this_vod.rgb_out_pixels[!this_vod.buffer_num] =
             malloc(this_hds->eye_output_width * 2 * RGB_OUT_SIZE * this_hds->eye_output_height);
         if (this_vod.rgb_out_pixels[!this_vod.buffer_num] == NULL) {
            LOG_ERROR("Unable to malloc rgb frame 0.");
            return 2;
         }
#ifdef ENCODE_TIMING
         start = SDL_GetTicks();
#endif
         if (SDL_RenderReadPixels(renderer, NULL, PIXEL_FORMAT_OUT,
                                  this_vod.rgb_out_pixels[!this_vod.buffer_num],
                                  this_hds->eye_output_width * 2 * RGB_OUT_SIZE) != 0) {
            LOG_ERROR("SDL_RenderReadPixels() failed: %s", SDL_GetError());
#ifdef ENCODE_TIMING
         } else {
            stop = SDL_GetTicks();
            cur_time = stop - start;
            avg_time = ((avg_time * weight) + cur_time) / (weight + 1);
            weight++;
            if (cur_time > max_time)
               max_time = cur_time;
            if ((cur_time < min_time) || (min_time == 0))
               min_time = cur_time;
            LOG_INFO("SDL_RenderReadPixels(): %0.2f ms, min: %d, max: %d. weight: %d",
                   avg_time, min_time, max_time, weight);
#endif
         }

         pthread_mutex_lock(&this_vod.p_mutex);
         if (this_vod.rgb_out_pixels[this_vod.buffer_num] != NULL) {
            free(this_vod.rgb_out_pixels[this_vod.buffer_num]);
         }
         this_vod.buffer_num = !this_vod.buffer_num;
         pthread_mutex_unlock(&this_vod.p_mutex);

         if (vid_out_thread == 0) {
            if (pthread_create(&vid_out_thread, NULL, video_next_thread, NULL) != 0) {
               LOG_ERROR("Error creating video output thread.");
               this_vod.output = 0;
            }
         }
      }

      SDL_Delay(33);
   }

   return 0;
}

/* Pthread function to background object detection. Runs to completion once per frame. */
void *object_detection_thread(void *arg)
{
   //int detects = 0;
   od_data *my_data = (od_data *) arg;

   detect_image(&my_data->detect_obj, my_data->pix_data, this_detect[my_data->eye], MAX_DETECT);
   //detects = detect_image(&my_data->detect_obj, my_data->pix_data, this_detect[my_data->eye], MAX_DETECT);
   //printf("Objects detected: %d\n", detects);

   my_data->complete = 1;

   return NULL;
}

/**
 * Builds a complete GStreamer pipeline string for stereo camera setup
 * @param descr Output buffer for the complete pipeline string
 * @param descr_size Size of the output buffer
 * @param cam_type Type of camera ("csi" or "usb")
 * @param this_hds Struct containing camera configuration parameters
 *
 * The function constructs separate pipelines for left and right cameras
 * based on the camera type and combines them into a single pipeline string.
 * For CSI cameras, sensor_id 0 is used for left and 1 for right.
 * For USB cameras, /dev/video0 is used for left and /dev/video2 for right.
 */
static void build_pipeline_string(char* descr, size_t descr_size, const char* cam_type,
                                  const hud_display_settings* this_hds) {
   char left_pipeline[GSTREAMER_PIPELINE_LENGTH/2];
   char right_pipeline[GSTREAMER_PIPELINE_LENGTH/2];
   bool is_csi = (cam_type == NULL) || (strncmp(cam_type, "csi", 3) == 0);

   if (is_csi) {
      if (cam1_id == -1) {
         cam1_id = DEFAULT_CSI_CAM1;
         cam2_id = DEFAULT_CSI_CAM2;
      }
      g_snprintf(left_pipeline, sizeof(left_pipeline), GST_CAM_PIPELINE_CSI_INPUT GST_CAM_PIPELINE_OUTPUT,
                 cam1_id, this_hds->cam_input_width, this_hds->cam_input_height,
                 this_hds->cam_input_fps, this_hds->cam_frame_duration,
                 single_cam ? "" : "L");

      if (!single_cam) {
         g_snprintf(right_pipeline, sizeof(right_pipeline), GST_CAM_PIPELINE_CSI_INPUT GST_CAM_PIPELINE_OUTPUT,
                    cam2_id, this_hds->cam_input_width, this_hds->cam_input_height,
                    this_hds->cam_input_fps, this_hds->cam_frame_duration, "R");
      }
   } else {
      if (cam1_id == -1) {
         cam1_id = DEFAULT_USB_CAM1;
         cam2_id = DEFAULT_USB_CAM2;
      }
      g_snprintf(left_pipeline, sizeof(left_pipeline), GST_CAM_PIPELINE_USB_INPUT GST_CAM_PIPELINE_OUTPUT,
                 cam1_id, this_hds->cam_input_width, this_hds->cam_input_height,
                 this_hds->cam_input_fps, this_hds->cam_frame_duration,
                 single_cam ? "" : "L");

      if (!single_cam) {
         g_snprintf(right_pipeline, sizeof(right_pipeline), GST_CAM_PIPELINE_USB_INPUT GST_CAM_PIPELINE_OUTPUT,
                    cam2_id, this_hds->cam_input_width, this_hds->cam_input_height,
                    this_hds->cam_input_fps, this_hds->cam_frame_duration, "R");
      }
   }

   if (single_cam) {
      g_snprintf(descr, descr_size, "%s", left_pipeline);
   } else {
      g_snprintf(descr, descr_size, "%s %s", left_pipeline, right_pipeline);
   }
}

/* Video input handling thread.
 * This thread handles input from the cameras and camera sync using timestamps.
 */
void *video_processing_thread(void *arg)
{
   GstElement *pipeline = NULL, *sinkL = NULL, *sinkR = NULL;
   GstSample *sampleL[2] = { NULL }, *sampleR[2] = { NULL };
   GstBuffer *bufferL[2] = { NULL }, *bufferR[2] = { NULL };
   gchar descr[GSTREAMER_PIPELINE_LENGTH] = "";
   GError *error = NULL;
   gboolean eosL = false, eosR = false;
   const char *cam_type = (const char *) arg;

#ifdef DEBUG_BUFFERS
   int sync_comp = 0;
#endif

   hud_display_settings *this_hds = get_hud_display_settings();

   /* build and start pipeline */
   build_pipeline_string(descr, sizeof(descr), cam_type, this_hds);

   pipeline = gst_parse_launch(descr, &error);
   if (error != NULL) {
      SDL_Log("could not construct pipeline: %s\n", error->message);
      g_error_free(error);
      return NULL;
   }

   /* get sink */
   if (single_cam) {
      sinkL = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
   } else {
      sinkL = gst_bin_get_by_name(GST_BIN(pipeline), "sinkL");
      sinkR = gst_bin_get_by_name(GST_BIN(pipeline), "sinkR");
   }
   // From here on out, we'll check sinkR to see if we need to process it.

   gst_element_set_state(pipeline, GST_STATE_PLAYING);

   while (!quit) {
      // Unmap buffers and unref the samples
      if (bufferL[!buffer_num] != NULL) {
         gst_buffer_unmap(bufferL[!buffer_num], &mapL[!buffer_num]);
      }
      if (sampleL[!buffer_num] != NULL) {
         gst_sample_unref(sampleL[!buffer_num]);
      }
      if (sinkR) {
         if (bufferR[!buffer_num] != NULL) {
            gst_buffer_unmap(bufferR[!buffer_num], &mapR[!buffer_num]);
         }
         if (sampleR[!buffer_num] != NULL) {
            gst_sample_unref(sampleR[!buffer_num]);
         }
      }

      /* get the preroll buffer from appsink */
      g_signal_emit_by_name(sinkL, "pull-sample", &sampleL[!buffer_num], NULL);
      if (sinkR) {
         g_signal_emit_by_name(sinkR, "pull-sample", &sampleR[!buffer_num], NULL);
      }

      if (sampleL[!buffer_num] == NULL) {
         eosL = gst_app_sink_is_eos(GST_APP_SINK(sinkL));
         if (eosL) {
            LOG_ERROR("sinkL returned NULL. It is EOS.");
            quit = 1;
            return NULL;
         } else {
            LOG_ERROR("sinkL returned NULL. It is NOT EOS!?!?");
            continue;
         }
      }

      if (sinkR) {
         if (sampleR[!buffer_num] == NULL) {
            eosR = gst_app_sink_is_eos(GST_APP_SINK(sinkR));
            if (eosR) {
               LOG_ERROR("sinkR returned NULL. It is EOS.");
               quit = 1;
               return NULL;
            } else {
               LOG_ERROR("sinkR returned NULL. It is NOT EOS!?!?");
               continue;
            }
         }
      }

#ifdef DISPLAY_TIMING
      clock_gettime(CLOCK_REALTIME, &ts_cap[!buffer_num]);
#endif

      /* if we have a buffer now, convert it to a pixbuf */
      /* We only need to sync left and right if they're from different cameras. */
      if (sinkR) {
         if (sampleL[!buffer_num] && sampleR[!buffer_num]) {
            /* get the pixbuf */
            bufferL[!buffer_num] = gst_sample_get_buffer(sampleL[!buffer_num]);
            bufferR[!buffer_num] = gst_sample_get_buffer(sampleR[!buffer_num]);

            /* handle sync */
            while (((long)bufferL[!buffer_num]->pts - (long)bufferR[!buffer_num]->pts) >
                  this_hds->cam_frame_duration) {
               gst_sample_unref(sampleR[!buffer_num]);
               g_signal_emit_by_name(sinkR, "pull-sample", &sampleR[!buffer_num], NULL);
               bufferR[!buffer_num] = gst_sample_get_buffer(sampleR[!buffer_num]);
#ifdef DEBUG_BUFFERS
               LOG_WARNING("Catching up R buffer.");
               LOG_WARNING("bufferL PTS: %lu, bufferR PTS: %lu, %10ld: %d, sync_comp: %d",
                     bufferL[!buffer_num]->pts, bufferR[!buffer_num]->pts,
                     (long)bufferL[!buffer_num]->pts - (long)bufferR[!buffer_num]->pts,
                     (this_hds->cam_frame_duration) >
                     abs((long)bufferL[!buffer_num]->pts - (long)bufferR[!buffer_num]->pts),
                     sync_comp);
               sync_comp++;
#endif
            }
            while (((long)bufferR[!buffer_num]->pts - (long)bufferL[!buffer_num]->pts) >
                  this_hds->cam_frame_duration) {
               gst_sample_unref(sampleL[!buffer_num]);
               g_signal_emit_by_name(sinkL, "pull-sample", &sampleL[!buffer_num], NULL);
               bufferL[!buffer_num] = gst_sample_get_buffer(sampleL[!buffer_num]);
#ifdef DEBUG_BUFFERS
               LOG_WARNING("Catching up L buffer.");
               LOG_WARNING("bufferL PTS: %lu, bufferR PTS: %lu, %10ld: %d, sync_comp: %d",
                     bufferL[!buffer_num]->pts, bufferR[!buffer_num]->pts,
                     (long)bufferL[!buffer_num]->pts - (long)bufferR[!buffer_num]->pts,
                     (this_hds->cam_frame_duration) >
                     abs((long)bufferL[!buffer_num]->pts - (long)bufferR[!buffer_num]->pts),
                     sync_comp);
               sync_comp++;
#endif
            }

#ifdef DEBUG_BUFFERS
            LOG_INFO("bufferL PTS: %lu, bufferR PTS: %lu, %10ld: %d, sync_comp: %d",
                  bufferL[!buffer_num]->pts, bufferR[!buffer_num]->pts,
                  (long)bufferL[!buffer_num]->pts - (long)bufferR[!buffer_num]->pts,
                  (this_hds->cam_frame_duration) >
                  abs((long)bufferL[!buffer_num]->pts - (long)bufferR[!buffer_num]->pts), sync_comp);
#endif

            gst_buffer_map(bufferL[!buffer_num], &mapL[!buffer_num], GST_MAP_READ);
            gst_buffer_map(bufferR[!buffer_num], &mapR[!buffer_num], GST_MAP_READ);

            pthread_mutex_lock(&v_mutex);
            video_posted = 1;
            buffer_num = !buffer_num;
            pthread_mutex_unlock(&v_mutex);
         } else {
            g_print("could not make snapshot\n");
         }
      } else {
         if (sampleL[!buffer_num]) {
            /* get the pixbuf */
            bufferL[!buffer_num] = gst_sample_get_buffer(sampleL[!buffer_num]);

            gst_buffer_map(bufferL[!buffer_num], &mapL[!buffer_num], GST_MAP_READ);

            pthread_mutex_lock(&v_mutex);
            video_posted = 1;
            buffer_num = !buffer_num;
            pthread_mutex_unlock(&v_mutex);
         } else {
            g_print("could not make snapshot\n");
         }
      }
   }

   /* Video */
   gst_element_set_state(pipeline, GST_STATE_NULL);
   gst_object_unref(pipeline);

   return NULL;
}

static int feed_me = 0;             /* Control the feeding of the encoding thread. */

/* This signal callback triggers when appsrc needs data. Here, we add an idle handler
 * to the mainloop to start pushing data into the appsrc */
static void start_feed (GstElement *source, guint size, void *data) {
   feed_me = 1;
}

/* This callback triggers when appsrc has enough data and we can stop sending.
 * We remove the idle handler from the mainloop */
static void stop_feed (GstElement *source, void *data) {
   feed_me = 0;
}

/* pthread function to control video output handling from pipeline to buffers. */
void *video_next_thread(void *arg)
{
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

   pthread_mutex_lock(&windowSizeMutex);
   local_window_width = window_width;
   local_window_height = window_height;
   pthread_mutex_unlock(&windowSizeMutex);

   /* We date code our recordings. */
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

   /* get sink */
   srcEncode = gst_bin_get_by_name(GST_BIN(pipeline), "srcEncode");

   g_signal_connect (srcEncode, "need-data", G_CALLBACK (start_feed), NULL);
   g_signal_connect (srcEncode, "enough-data", G_CALLBACK (stop_feed), NULL);

   /* set the caps on the source */
   caps = gst_caps_new_simple ("video/x-raw",
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

            /* get the preroll buffer from appsink */
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
      // Calculate processing time in microseconds
      processing_time_us = (end_time.tv_sec - start_time.tv_sec) * 1000000L + (end_time.tv_nsec - start_time.tv_nsec) / 1000L;

      // Calculate how long to delay to maintain the target frame rate
      delay_time_us = TARGET_RECORDING_FRAME_DURATION_US - processing_time_us;

      // Apply the calculated delay, if positive
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

   //gst_bus_poll(bus, GST_MESSAGE_EOS, GST_CLOCK_TIME_NONE);
   gst_object_unref (bus);
   gst_object_unref(pipeline);

   vid_out_thread = 0;

   return NULL;
}

/* Search the font list to see if this font has already been created.
 * If so, return it.
 * If not, create it and return it.
 */
TTF_Font *get_local_font(char *font_name, int font_size)
{
   local_font *this_font = NULL;

   if (font_list == NULL) {
      font_list = malloc(sizeof(local_font));
      if (font_list == NULL) {
         return NULL;
      }
      this_font = font_list;
   } else {
      /* Search for existing allocated font. */
      this_font = font_list;
      while (this_font != NULL) {
         if ((strcmp(font_name, this_font->font) == 0) && (font_size == this_font->font_size)) {
            return this_font->ttf_font;
         }

         if (this_font->next == NULL) {
            break;
         } else {
            this_font = this_font->next;
         }
      }

      /* None found. Create new one. */
      this_font->next = malloc(sizeof(local_font));
      if (this_font->next == NULL) {
         return NULL;
      }
      this_font = this_font->next;
   }

   this_font->ttf_font = TTF_OpenFont(font_name, font_size);
   if (!this_font->ttf_font) {
      SDL_Log("Error loading font: %s\n", TTF_GetError());
      free(this_font);
      return NULL;
   }

   strncpy(this_font->font, font_name, MAX_FILENAME_LENGTH * 2);
   this_font->font_size = font_size;
   this_font->next = NULL;

   return this_font->ttf_font;
}

/* Render a single eye overlay onto both eyes. */
void renderStereo(SDL_Texture *tex, SDL_Rect *src, SDL_Rect *dest,
                  SDL_Rect *dest2, double angle)
{
   SDL_Rect src_rect_l, src_rect_r;
   SDL_Rect dest_rect_l, dest_rect_r;
   int overage = 0;
   double scale = 1.0;

   hud_display_settings *this_hds = get_hud_display_settings();

   if (dest == NULL) {
      LOG_ERROR("ERROR: renderStereo() was passed a NULL dest value!");

      return;
   }

   if (src == NULL) {
      SDL_QueryTexture(tex, NULL, NULL, &src_rect_l.w, &src_rect_l.h);
      src_rect_l.x = src_rect_l.y = 0;

      memcpy(&src_rect_r, &src_rect_l, sizeof(SDL_Rect));
   } else {
      memcpy(&src_rect_l, src, sizeof(SDL_Rect));
      memcpy(&src_rect_r, src, sizeof(SDL_Rect));
   }

   memcpy(&dest_rect_l, dest, sizeof(SDL_Rect));
   if (dest2 == NULL) {
      memcpy(&dest_rect_r, dest, sizeof(SDL_Rect));
   } else {
      memcpy(&dest_rect_r, dest2, sizeof(SDL_Rect));
   }
   dest_rect_r.x += this_hds->eye_output_width;

   /* Handle bounds for left eye */

   /* Left edge of left eye */
   if (dest_rect_l.x < 0) {
      if (src_rect_l.w != dest_rect_l.w) {
         scale = ((double) src_rect_l.w) / ((double) dest_rect_l.w);
      }

      overage = -dest_rect_l.x;
      src_rect_l.x += (scale * overage);
      src_rect_l.w -= (scale * overage);
      dest_rect_l.w -= overage;
      dest_rect_l.x = 0;
   }

   /* Right edge of left eye */
   if ((dest_rect_l.x + dest_rect_l.w) > this_hds->eye_output_width) {
      if (src_rect_l.w != dest_rect_l.w) {
         scale = ((double) src_rect_l.w) / ((double) dest_rect_l.w);
      }

      overage = (dest_rect_l.x + dest_rect_l.w) - this_hds->eye_output_width;
      dest_rect_l.w = dest_rect_l.w - overage;
      src_rect_l.w = src_rect_l.w - (scale * overage);
   }

   /* Top edge of left eye */
   if (dest_rect_l.y < 0) {
      if (src_rect_l.h != dest_rect_l.h) {
         scale = ((double) src_rect_l.h) / ((double) dest_rect_l.h);
      }

      overage = -dest_rect_l.y;
      src_rect_l.y += (scale * overage);
      src_rect_l.h -= (scale * overage);
      dest_rect_l.h -= overage;
      dest_rect_l.y = 0;
   }

   /* Bottom edge of left eye */
   if ((dest_rect_l.y + dest_rect_l.h) > this_hds->eye_output_height) {
      if (src_rect_l.h != dest_rect_l.h) {
         scale = ((double) src_rect_l.h) / ((double) dest_rect_l.h);
      }

      overage = (dest_rect_l.y + dest_rect_l.h) - this_hds->eye_output_height;
      dest_rect_l.h = dest_rect_l.h - overage;
      src_rect_l.h = src_rect_l.h - (scale * overage);
   }

   /* Handle bounds for right eye */

   /* Left edge of right eye */
   if (dest_rect_r.x < this_hds->eye_output_width) {
      if (src_rect_r.w != dest_rect_r.w) {
         scale = ((double) src_rect_r.w) / ((double) dest_rect_r.w);
      }

      overage = this_hds->eye_output_width - dest_rect_r.x;
      src_rect_r.x += (scale * overage);
      src_rect_r.w -= (scale * overage);
      dest_rect_r.w -= overage;
      dest_rect_r.x = this_hds->eye_output_width;
   }

   /* Right edge of right eye */
   if ((dest_rect_r.x + dest_rect_r.w) > (2 * this_hds->eye_output_width)) {
      if (src_rect_r.w != dest_rect_r.w) {
         scale = ((double) src_rect_r.w) / ((double) dest_rect_r.w);
      }

      overage = (dest_rect_r.x + dest_rect_r.w) - (2 * this_hds->eye_output_width);
      dest_rect_r.w -= overage;
      src_rect_r.w -= (scale * overage);
   }

   /* Top edge of right eye */
   if (dest_rect_r.y < 0) {
      if (src_rect_r.h != dest_rect_r.h) {
         scale = ((double) src_rect_r.h) / ((double) dest_rect_r.h);
      }

      overage = -dest_rect_r.y;
      src_rect_r.y += (scale * overage);
      src_rect_r.h -= (scale * overage);
      dest_rect_r.h -= overage;
      dest_rect_r.y = 0;
   }

   /* Bottom edge of right eye */
   if ((dest_rect_r.y + dest_rect_r.h) > this_hds->eye_output_height) {
      if (src_rect_r.h != dest_rect_r.h) {
         scale = ((double) src_rect_r.h) / ((double) dest_rect_r.h);
      }

      overage = (dest_rect_r.y + dest_rect_r.h) - this_hds->eye_output_height;
      dest_rect_r.h = dest_rect_r.h - overage;
      src_rect_r.h = src_rect_r.h - (scale * overage);
   }

   /* Render each eye independently */
   /* Left eye */
   if (dest_rect_l.w > 0 && dest_rect_l.h > 0 && src_rect_l.w > 0 && src_rect_l.h > 0) {
      if (!angle) {
         SDL_RenderCopy(renderer, tex, &src_rect_l, &dest_rect_l);
      } else {
         SDL_RenderCopyEx(renderer, tex, &src_rect_l, &dest_rect_l, angle, NULL, SDL_FLIP_NONE);
      }
   }

   /* Right eye */
   if (dest_rect_r.w > 0 && dest_rect_r.h > 0 && src_rect_r.w > 0 && src_rect_r.h > 0) {
      if (!angle) {
         SDL_RenderCopy(renderer, tex, &src_rect_r, &dest_rect_r);
      } else {
         SDL_RenderCopyEx(renderer, tex, &src_rect_r, &dest_rect_r, angle, NULL, SDL_FLIP_NONE);
      }
   }
}

/**
 * Sends a text-to-speech command via MQTT, instructing a device to vocalize the provided text.
 *
 * Constructs a JSON-formatted MQTT command specifying the text to be converted to speech.
 * This command is published to the MQTT topic "dawn". It checks for an initialized MQTT
 * client (`mosq`) before publishing and reports any errors encountered during the process.
 *
 * @param text The text string to be vocalized by the text-to-speech device.
 *
 * Note:
 * - The MQTT client (`mosq`) must be initialized and connected prior to calling this function.
 * - Errors during publishing are reported with a descriptive message.
 */
void mqttTextToSpeech(const char *text) {
   char mqtt_command[1024] = "";
   int rc = 0;

   // Construct the MQTT command with the provided text
   snprintf(mqtt_command, sizeof(mqtt_command),
            "{ \"device\": \"text to speech\", \"action\": \"play\", \"value\": \"%s\" }",
            text);

   if (mosq == NULL) {
      LOG_ERROR("MQTT not initialized.");
   } else {
      rc = mosquitto_publish(mosq, NULL, "dawn", strlen(mqtt_command), mqtt_command, 0, false);
      if (rc != MOSQ_ERR_SUCCESS) {
         LOG_ERROR("Error publishing: %s", mosquitto_strerror(rc));
      }
   }
}

/**
 * Notifies the "dawn" process that a "viewing" command has completed, providing a snapshot
 * filename for processing.
 *
 * Constructs and publishes a JSON-formatted message to the MQTT topic "dawn". This message
 * indicates that a viewing command has been executed and includes the filename of the snapshot
 * generated as a result. The function checks for an initialized MQTT client (`mosq`) before
 * attempting to publish and reports errors if publishing fails.
 *
 * @param filename The filename of the snapshot generated by the viewing command.
 *
 * Note:
 * - Ensure that the MQTT client (`mosq`) is initialized and connected before calling this function.
 * - Failure to publish the message will result in an error printed to stderr with the failure reason.
 */
void mqttViewingSnapshot(const char *filename) {
   char mqtt_command[1024] = "";
   int rc = 0;

   // Construct the MQTT command with the snapshot filename
   snprintf(mqtt_command, sizeof(mqtt_command),
      "{ \"device\": \"viewing\", \"action\": \"completed\", \"value\": \"%s\" }",
      filename);

   if (mosq == NULL) {
      LOG_ERROR("MQTT not initialized.");
   } else {
      rc = mosquitto_publish(mosq, NULL, "dawn", strlen(mqtt_command), mqtt_command, 0, false);
      if (rc != MOSQ_ERR_SUCCESS) {
         LOG_ERROR("Error publishing: %s", mosquitto_strerror(rc));
      }
   }
}

/**
 * @brief Computes the scaled window size while maintaining the original aspect ratio.
 *
 * This function calculates the appropriate window width and height based on the current desktop
 * resolution and the native resolution of the application. It ensures that the window size fits
 * within the desktop dimensions without distorting the original aspect ratio.
 *
 * This function was created to make sure full screen is actually full screen.
 *
 * @param desktop_width  The width of the desktop display in pixels.
 * @param desktop_height The height of the desktop display in pixels.
 * @param native_width   The native width of the application window in pixels.
 * @param native_height  The native height of the application window in pixels.
 * @param window_width   Pointer to an integer where the computed window width will be stored.
 * @param window_height  Pointer to an integer where the computed window height will be stored.
 *
 * @return
 * - `0` on successful computation.
 * - `1` if an error occurs (e.g., invalid parameters).
 *
 * @note
 * - The function compares the aspect ratios of the desktop and native resolutions to determine
 *   whether to scale based on width or height.
 * - It ensures that the resulting window size does not exceed the desktop resolution.
 *
 * @warning
 * - Both `window_width` and `window_height` must be valid, non-null pointers.
 * - Ensure that `native_width` and `native_height` are greater than zero to avoid division by zero.
 *
 * @example
 * @code
 * int desktopW = 1920;
 * int desktopH = 1080;
 * int nativeW = 2880;
 * int nativeH = 1440;
 * int scaledW, scaledH;
 *
 * if (computeScaledWindowSize(desktopW, desktopH, nativeW, nativeH, &scaledW, &scaledH) == 0) {
 *     // Proceed with using scaledW and scaledH
 * } else {
 *     // Handle the error
 * }
 * @endcode
 */
int computeScaledWindowSize(int desktop_width, int desktop_height,
                            int native_width, int native_height,
                            int* window_width, int* window_height) {
   // Validate pointer parameters
   if (window_width == NULL || window_height == NULL) {
      fprintf(stderr, "Error: window_width and window_height pointers must not be NULL.\n");
      return 1;
   }

   // Validate native dimensions
   if (desktop_width <= 0 || desktop_height <= 0 || native_width <= 0 || native_height <= 0) {
      fprintf(stderr, "Error: All passed widths and heights must be greater than zero.\n");
      return 1;
   }

   // Calculate aspect ratios
   float native_aspect = (float)native_width / (float)native_height;
   float desktop_aspect = (float)desktop_width / (float)desktop_height;

   if (desktop_aspect > native_aspect) {
      // Desktop is wider than native aspect ratio
      *window_height = desktop_height;
      *window_width = (int)(desktop_height * native_aspect);
   } else {
      // Desktop is taller or equal to native aspect ratio
      *window_width = desktop_width;
      *window_height = (int)(desktop_width / native_aspect);
   }

   return 0; // Success
}

// Two PBOs for double buffering
static GLuint g_pboIds[2] = {0, 0};
static int g_pboIndex = 0;
static bool g_pboInitialized = false;

/**
 * @brief Asynchronously reads pixels from the current OpenGL framebuffer into a user buffer
 *        using a double-buffered Pixel Buffer Object (PBO) approach.
 *
 * This function is somewhat analogous to SDL_RenderReadPixels(), but uses asynchronous
 * PBO transfers to reduce CPU-GPU stalls. It currently assumes RGBA-8-bit format and uses
 * GL_RGBA / GL_UNSIGNED_BYTE for reading.
 *
 * @param[in] renderer  Pointer to the SDL_Renderer (must be using an OpenGL backend).
 * @param[in] rect      Optional rectangle specifying the area to read. If NULL, the entire
 *                      render output is read.
 * @param[in] format    SDL pixel format (currently not deeply used; assumes RGBA).
 * @param[out] pixels   Pointer to the user-allocated buffer where pixels will be copied.
 * @param[in] pitch     Byte pitch (row stride) of the user buffer, typically (width * 4).
 *
 * @return int  Returns 0 on success, 1 on failure.
 */
int OpenGL_RenderReadPixelsAsync(SDL_Renderer *renderer,
                                const SDL_Rect *rect,
                                Uint32 format,
                                void *pixels,
                                int pitch)
{
   // Basic parameter checks
   if (!renderer || !pixels) {
      LOG_ERROR("Invalid arguments: renderer=%p, pixels=%p",
                (void*)renderer, (void*)pixels);
      return 1;
   }

   // Grab current GL context from SDL (assumes we've made it current).
   SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
   if (!currentContext) {
      LOG_ERROR("No current GL context found.");
      return 1;
   }

   // Determine the rectangle to read
   int readX = 0, readY = 0, readW = 0, readH = 0;
   if (rect) {
      readX = rect->x;
      readY = rect->y;
      readW = rect->w;
      readH = rect->h;
   } else {
      // If rect is NULL, read the entire render target
      SDL_GetRendererOutputSize(renderer, &readW, &readH);
   }

   // Lazy-init the PBOs if needed
   if (!g_pboInitialized) {
      glGenBuffers(2, g_pboIds);
      g_pboInitialized = true;
      LOG_INFO("Generated two PBOs for double-buffered readback.");
   }

   // Calculate the data size (assuming RGBA 8-bit).
   const int bytesPerPixel = 4;  // RGBA
   const GLsizeiptr dataSize = (GLsizeiptr)(readW * readH * bytesPerPixel);

   // Bind the “current” PBO for asynchronous readback
   glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[g_pboIndex]);
   glBufferData(GL_PIXEL_PACK_BUFFER, dataSize, NULL, GL_STREAM_READ);

   // Kick off the async read from the current framebuffer
   glReadPixels(readX, readY, readW, readH, GL_RGBA, GL_UNSIGNED_BYTE, 0);

   // Now bind the “previous” PBO and try to map it to system memory
   int prevIndex = (g_pboIndex + 1) % 2;
   glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pboIds[prevIndex]);

   GLubyte* mappedBuffer = (GLubyte*)glMapBufferRange(GL_PIXEL_PACK_BUFFER,
                                                      0,
                                                      dataSize,
                                                      GL_MAP_READ_BIT);

   if (mappedBuffer) {
      // Copy the pixel data into the user-provided buffer
      // For simplicity, we assume pitch == width * 4. Adjust if needed.
      for (int y = 0; y < readH; ++y) {
         int flippedY = (readH - 1) - y; // Invert the row index
         GLubyte* dstRow = (GLubyte*)pixels + (flippedY * pitch);
         GLubyte* srcRow = mappedBuffer + (y * readW * bytesPerPixel);
         memcpy(dstRow, srcRow, readW * bytesPerPixel);
      }

      // Unmap after copying
      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
   } else {
      LOG_WARNING("Mapped buffer was NULL. Possible missed frame or GPU is still busy?");
   }

   // Unbind the PBO
   glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

   // Flip the index so next call reads the other buffer
   g_pboIndex = prevIndex;

   return 0;
}

void display_help(int argc, char *argv[]) {
   if (argc > 0) {
      printf("Usage: %s [options]\n", argv[0]);
   } else {
      printf("Usage: [options]\n");  // Fallback if command name is not available
   }

   printf("Options:\n");
   printf("  -f, --fullscreen       Run in fullscreen mode.\n");
   printf("  -h, --help             Display this help message and exit.\n");
   printf("  -l, --logfile LOGFILE  Specify the log filename instead of stdout/stderr.\n");
   printf("\n");
   printf("  -p, --record_path PATH Specify the path for recordings.\n");
   printf("  -r, --record           Start recording on startup.\n");
   printf("  -s, --stream           Start streaming on startup.\n");
   printf("  -t, --record_stream    Start both recording and streaming on startup.\n");
   printf("\n");
   printf("  -u, --usb              Connect via USB/serial.\n");
   printf("  -d, --device DEVICE    Specify the device for USB/serial connection.\n");
   printf("  -c, --camera TYPE      Specify the camera type, csi or usb.\n");
   printf("  -n, --camcount [1/2]   Specify the number of cameras for display, 1 or 2.\n");
   printf("\n");
}

/* MAIN: Start here. */
int main(int argc, char **argv)
{
   /* Variable Inits */
   hud_display_settings *this_hds = get_hud_display_settings();
   armor_settings *this_as = get_armor_settings();

   /* Graphics */
   SDL_Window *window = NULL;
   SDL_DisplayMode desktop_mode;
   int desktop_width = 0, desktop_height = 0;
   window_width = this_hds->eye_output_width * 2;
   window_height = this_hds->eye_output_height;
   int native_width = this_hds->eye_output_width * 2;
   int native_height = this_hds->eye_output_height;
   Uint32 sdl_flags = 0;
   int curr_fps = 0;
   SDL_Event event;

   element *curr_element = NULL;

   /* getopt */
   int opt = 0;
   int fullscreen = 0;

   /* Threads */
   pthread_t thread_handles[NUM_AUDIO_THREADS];
   int current_thread = 0;
   pthread_t video_proc_thread = 0;

   /* Serial Port */
   char usb_enable = 0;
   char usb_port[24] = USB_PORT;

   int rc = -1;

   pthread_t command_proc_thread = 0;

   /* Video */
   SDL_Texture *textureL = NULL, *textureR = NULL;

#ifdef DISPLAY_TIMING
   unsigned long last_ts_cap = 0, present_time = 0, ts_total = 0;
   unsigned int ts_count = 0;
   struct timespec display_time = { .tv_sec = 0, .tv_nsec = 0};
#endif

   this_vod.outfile = NULL;
   this_vod.rgb_out_pixels[0] = NULL;
   this_vod.rgb_out_pixels[1] = NULL;
   pthread_mutex_init(&this_vod.p_mutex, NULL);
   this_vod.buffer_num = 0;
   this_vod.output = 0;
   this_vod.filename[0] = '\0';
   this_vod.started = 0;
   vid_out_thread = 0;

   local_font *this_font = NULL;

   unsigned int totalFrames = 0;
   unsigned int currTime = SDL_GetTicks();
   unsigned int last_file_check = 0;

   Uint64 thisPTime, lastPTime;
   double elapsed = 0.0;

#ifdef ENCODE_TIMING
   int cur_time = 0, max_time = 0, min_time = 0, weight = 0;
   double avg_time = 0.0;
#endif

   FrameRateTracker tracker;
   initializeFrameRateTracker(&tracker);

   char config_file[] = "config.json";

   int intro_finished = 0;

   off_t last_size = -1;
   off_t last_last_size = -1;
   /* End Variable Inits */

   sdl_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI;

   /*
    * Process Command Line
    * c: - camera type, usb/csi
    * f  - fullscreen
    * h  - help text
    * l  - log filename
    * n: - number of cameras
    * p: - path for recordings
    * r  - record on startup
    * s  - stream on startup
    * t  - record and stream on startup
    * u: - USB/serial with port
    */
   static struct option long_options[] = {
      {"camera", required_argument, NULL, 'c'},
      {"device", required_argument, NULL, 'd'},
      {"fullscreen", no_argument, NULL, 'f'},
      {"help", no_argument, NULL, 'h'},
      {"logfile", required_argument, NULL, 'l'},
      {"camcount", required_argument, NULL, 'n'},
      {"record_path", required_argument, NULL, 'p'},
      {"record", no_argument, NULL, 'r'},
      {"stream", no_argument, NULL, 's'},
      {"record_stream", no_argument, NULL, 't'},
      {"usb", no_argument, NULL, 'u'},
      {0, 0, 0, 0}
   };
   int option_index = 0;

   const char *log_filename = NULL;
   const char *cam_type = NULL;

   printf("%s Version %s: %s\n", APP_NAME, VERSION_NUMBER, GIT_SHA);

   if (SDL_Init(SDL_INIT_VIDEO) == -1) {
      SDL_Log("SDL_Init(SDL_INIT_VIDEO) failed: %s\n", SDL_GetError());
      return EXIT_FAILURE;
   }

   if (getcwd(record_path, sizeof(record_path)) == NULL) {
      LOG_ERROR("getcwd() error!");

      return EXIT_FAILURE;
   }

   while (1) {
      opt = getopt_long(argc, argv, "c:d:fhl:n:p:rstu", long_options, &option_index);

      if (opt == -1) {
         break;
      }

      switch (opt) {
      case 'c':
         if ((strncmp(optarg, "usb", 3) != 0) && (strncmp(optarg, "csi", 3) != 0)) {
            fprintf(stderr, "Camera type must be \"usb\" or \"csi\".\n");
            return EXIT_FAILURE;
         }

         cam_type = optarg;
         break;
      case 'f':
         fullscreen = 1;
         sdl_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

         if (SDL_GetDesktopDisplayMode(0, &desktop_mode) != 0) {
            fprintf(stderr, "SDL_GetDesktopDisplayMode failed: %s\n", SDL_GetError());
            SDL_Quit();
            return EXIT_FAILURE;
         }

         desktop_width = desktop_mode.w;
         desktop_height = desktop_mode.h;

         if (computeScaledWindowSize(desktop_width, desktop_height, native_width, native_height,
                                     &window_width, &window_height) == 1) {
            fprintf(stderr, "computeScaledWindowSize() failed!\n");
            return EXIT_FAILURE;
         }

         break;
      case 'h':
         display_help(argc, argv);
         return EXIT_SUCCESS;
      case 'l':
         log_filename = optarg;
         break;
      case 'n':
         char *token = NULL;
         int values[3] = {-1, -1, -1}; // Store camcount and optional camera IDs
         int index = 0;

         // Create a copy of optarg since strtok modifies the string
         char *optarg_copy = strdup(optarg);
         if (!optarg_copy) {
            fprintf(stderr, "Memory allocation failed!\n");
            return EXIT_FAILURE;
         }

         // Parse comma-separated values
         token = strtok(optarg_copy, ",");
         while (token && index < 3) {
            values[index] = atoi(token);
            token = strtok(NULL, ",");
            index++;
         }

         // Validate camera count
         if (values[0] != 1 && values[0] != 2) {
            fprintf(stderr, "camcount (number of cameras) must be 1 or 2!\n");
            free(optarg_copy);
            return EXIT_FAILURE;
         }

         single_cam = (values[0] == 1);

         // Handle camera IDs if provided
         if (values[0] == 2) {
            if (index > 1) {
               cam1_id = values[1]; // First camera ID
            }
            if (index > 2) {
               cam2_id = values[2]; // Second camera ID
            }
         } else if (values[0] == 1 && index > 1) {
            cam1_id = values[1]; // Single camera ID
         }

         free(optarg_copy);
         break;
      case 'p':
         snprintf(record_path, 256, "%s", optarg);
         break;
      case 'r':
         if (!this_vod.output) {
            this_vod.output = RECORD;
         }
         break;
      case 's':
         if (!this_vod.output) {
            this_vod.output = STREAM;
         }
         break;
      case 't':
         if (!this_vod.output) {
            this_vod.output = RECORD_STREAM;
         }
         break;
      case 'u':
         usb_enable = 1;
         serial_set_state(1, NULL, -1);
         break;
      case 'd':
         strncpy(usb_port, optarg, 24);
         serial_set_state(-1, usb_port, -1);
         break;
      default:
         display_help(argc, argv);
         return EXIT_FAILURE;
      }
   }

   // Initialize logging
   if (log_filename) {
      if (init_logging(log_filename, LOG_TO_FILE) != 0) {
         fprintf(stderr, "Failed to initialize logging to file: %s\n", log_filename);
         return EXIT_FAILURE;
      }
   } else {
      if (init_logging(NULL, LOG_TO_CONSOLE) != 0) {
         fprintf(stderr, "Failed to initialize logging to console\n");
         return EXIT_FAILURE;
      }
   }

   curl_global_init(CURL_GLOBAL_DEFAULT);

   /* Create server message queue */
   struct mq_attr attr;

   attr.mq_flags = 0;
   attr.mq_maxmsg = MAX_MESSAGES;
   attr.mq_msgsize = MAX_MSG_SIZE;
   attr.mq_curmsgs = 0;

   if (mq_unlink(SERVER_QUEUE_NAME) == -1) {
      // Checking but failure isn't important.
      //perror("Server: mq_unlink");
   }

   if ((qd_server = mq_open(SERVER_QUEUE_NAME, O_RDONLY | O_CREAT, QUEUE_PERMISSIONS, &attr)) == -1) {
      perror("Server: mq_open (server)");
      return EXIT_FAILURE;
   }

   /* If we don't get an argument, read from stdin. */
   if (!usb_enable) {
      LOG_WARNING("No serial port reading from stdin.");
   }

   for (current_thread = 0; current_thread < NUM_AUDIO_THREADS; current_thread++) {
      /* Setup Output Device */
      snprintf(audio_threads[current_thread].client_queue_name,
               MAX_FILENAME_LENGTH, "/stark-sound-client-%d", current_thread);
      if (mq_unlink(audio_threads[current_thread].client_queue_name) == -1) {
         // Checking but failure isn't important.
         //perror("Client: mq_unlink");
      }
      /* Create client message queue/read */
      if ((audio_threads[current_thread].qd_client =
           mq_open(audio_threads[current_thread].client_queue_name,
                   O_RDONLY | O_CREAT, QUEUE_PERMISSIONS, &attr)) == -1) {
         perror("Client: mq_open (client)");
      }
      /* server side */
      if ((qd_clients[current_thread] =
           mq_open(audio_threads[current_thread].client_queue_name, O_WRONLY)) == 1) {
         perror("Server: Not able to open client queue");
      }
      if ((audio_threads[current_thread].qd_server = mq_open(SERVER_QUEUE_NAME, O_WRONLY)) == -1) {
         perror("Client: mq_open (server)");
      }

      /* Create Thread */
      audio_threads[current_thread].thread_id = current_thread;
      //strncpy( audio_threads[current_thread].filename, file, MAX_FILENAME_LENGTH );
      //audio_threads[current_thread].start_percent = start_percent;
      audio_threads[current_thread].stop = 1;

      if (pthread_create
          (&thread_handles[current_thread], NULL, audio_thread, &audio_threads[current_thread])) {
         LOG_ERROR("Error creating thread [%d]", current_thread);
         return EXIT_FAILURE;
      }
   }

   /* Send test messages */
#ifdef STARTUP_SOUND
   process_audio_command(SOUND_PLAY, STARTUP_SOUND, 0.0);
#endif

   if (IMG_Init(IMG_INIT_PNG) < 0) {
      SDL_Log("Error initializing SDL_image: %s\n", IMG_GetError());
      return EXIT_FAILURE;
   }

   if (TTF_Init() < 0) {
      SDL_Log("Error initializing SDL_ttf: %s\n", TTF_GetError());
      return EXIT_FAILURE;
   }

   SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
   if ((window =
        SDL_CreateWindow(argv[0], SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         window_width, window_height, sdl_flags)) == NULL) {
      SDL_Log("SDL_CreateWindow() failed: %s\n", SDL_GetError());
      return EXIT_FAILURE;
   }

   // Create GL context
   SDL_GLContext glContext = SDL_GL_CreateContext(window);
   if (!glContext) {
      LOG_ERROR("Failed to create GL context: %s", SDL_GetError());
      // Handle error ...
      return EXIT_FAILURE;
   }

   // Make the context current
   if (SDL_GL_MakeCurrent(window, glContext) < 0) {
      LOG_ERROR("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
      // Handle error ...
      return EXIT_FAILURE;
   }

   // Initialize GLEW (if you're using GLEW)
   GLenum glewError = glewInit();
   if (GLEW_OK != glewError) {
      LOG_ERROR("GLEW Error: %s", glewGetErrorString(glewError));
      return EXIT_FAILURE;
   }

#ifdef REFRESH_SYNC
   if ((renderer =
        SDL_CreateRenderer(window, -1,
                           SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED)) == NULL) {
#else
   if ((renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED)) == NULL) {
#endif
      SDL_Log("SDL_CreateRenderer() failed: %s\n", SDL_GetError());
      return EXIT_FAILURE;
   }

   /* Init detect array */
   for (int j = 0; j < MAX_DETECT; j++) {
      this_detect[0][j].active = 0;
      this_detect[1][j].active = 0;
   }

   init_hud_manager();

   intro_element.enabled = 0;

   if (parse_json_config(config_file) == FAILURE) {
      LOG_ERROR("Failed to parse config file. Exiting.");
      return EXIT_FAILURE;
   }

   /* Now that we've parsed the config, set the window size to requested. */
   window_width = this_hds->eye_output_width * 2;
   window_height = this_hds->eye_output_height;
   SDL_SetWindowSize(window, window_width, window_height);

   // Set the logical size to your native resolution
   if (SDL_RenderSetLogicalSize(renderer, this_hds->eye_output_width * 2, this_hds->eye_output_height) != 0) {
      SDL_Log("Could not set logical size: %s", SDL_GetError());
   }

   /* Video Setup */
   if (fullscreen) {
      SDL_ShowCursor(0);
   }

#ifndef ORIGINAL_RATIO
   SDL_Rect v_src_rect = { this_hds->cam_crop_x, 0, this_hds->cam_crop_width, this_hds->cam_input_height };
   SDL_Rect v_dst_rectL = { 0, 0, this_hds->eye_output_width, this_hds->eye_output_height };
   SDL_Rect v_dst_rectR = { this_hds->eye_output_width, 0, this_hds->eye_output_width, this_hds->eye_output_height };
#else
   double input_ratio = (double) this_hds->cam_input_height / this_hds->cam_input_width;
   SDL_Rect v_src_rect = { 0, 0, this_hds->cam_input_width, this_hds->cam_input_height };
   SDL_Rect v_dst_rectL = { 0, (this_hds->eye_output_height-(this_hds->eye_output_height*input_ratio))/2,
                            this_hds->eye_output_width, this_hds->eye_output_height*input_ratio };
   SDL_Rect v_dst_rectR = { this_hds->eye_output_width,
                            (this_hds->eye_output_height-(this_hds->eye_output_height*input_ratio))/2,
                            this_hds->eye_output_width, this_hds->eye_output_height*input_ratio };
#endif


   textureL =
       SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                         this_hds->cam_input_width, this_hds->cam_input_height);
   if (textureL == NULL) {
      SDL_Log("SDL_CreateTexture() failed on textureL: %s\n", SDL_GetError());
      return EXIT_FAILURE;
   }
   textureR =
       SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                         this_hds->cam_input_width, this_hds->cam_input_height);
   if (textureR == NULL) {
      SDL_Log("SDL_CreateTexture() failed on textureR: %s\n", SDL_GetError());
      return EXIT_FAILURE;
   }

   /* Draw a background pattern in case the image has transparency */
   SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
   SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
   //SDL_RenderClear(renderer);

   gst_init(&argc, &argv);

   if (intro_element.enabled) {
      play_intro(30, 1, NULL);
   }

   mosquitto_lib_init();

   mosq = mosquitto_new(NULL, true, NULL);
   if(mosq == NULL){
      LOG_ERROR("Error: Out of memory.");
      return EXIT_FAILURE;
   }

   /* Configure callbacks. This should be done before connecting ideally. */
   mosquitto_connect_callback_set(mosq, on_connect);
   mosquitto_subscribe_callback_set(mosq, on_subscribe);
   mosquitto_message_callback_set(mosq, on_message);

   /* Connect to local MQTT server. */
   //rc = mosquitto_connect(mosq, "192.168.10.1", 1883, 60);
   rc = mosquitto_connect(mosq, "127.0.0.1", 1883, 60);
   if(rc != MOSQ_ERR_SUCCESS){
      mosquitto_destroy(mosq);
      LOG_ERROR("Error: %s", mosquitto_strerror(rc));
      return EXIT_FAILURE;
   }

   /* Start processing MQTT events. */
   mosquitto_loop_start(mosq);
   /* End Setup */

   if (intro_element.enabled) {
      play_intro(15, 1, NULL);
   }

   //od_data oddataL, oddataR;
   oddataL.complete = 1;
   oddataL.processed = 1;
   oddataL.pix_data = NULL;
   oddataR.complete = 1;
   oddataR.processed = 1;
   oddataR.pix_data = NULL;
   detect_enabled = 0;     /* Disabling due to bug. */
   if (detect_enabled) {
      if (init_detect(&oddataL.detect_obj, argc, argv, this_hds->cam_input_width, this_hds->cam_input_height))
      {
         LOG_ERROR("Error initializing detect!!!");
         detect_enabled = 0;
      } else {
         if (intro_element.enabled) {
            play_intro(15, 1, NULL);
         }
         if (init_detect(&oddataR.detect_obj, argc, argv, this_hds->cam_input_width, this_hds->cam_input_height))
         {
            LOG_ERROR("Error initializing detect!!!");
            detect_enabled = 0;
         } else {
            //oddataL.detect_obj.v_mutex = &v_mutex;
            //oddataR.detect_obj.v_mutex = &v_mutex;
         }
      }
   }

   if (pthread_create(&video_proc_thread, NULL, video_processing_thread, (void *) cam_type) != 0) {
      LOG_ERROR("Error creating video processing thread.");
      return EXIT_FAILURE;
   }

   lastPTime = SDL_GetPerformanceCounter();

   if (intro_element.enabled) {
      play_intro(15, 1, NULL);
   }

   if (!usb_enable) {
      strcpy(usb_port, "");

      if (pthread_create(&command_proc_thread, NULL, socket_command_processing_thread, NULL) != 0) {
         LOG_ERROR("Error creating command processing thread.");
         return EXIT_FAILURE;
      }
   } else {
      if (pthread_create(&command_proc_thread, NULL, serial_command_processing_thread, (void *) usb_port) != 0) {
         LOG_ERROR("Error creating command processing thread.");
         return EXIT_FAILURE;
      }
   }

   mqttTextToSpeech("Your hud is now online boss.");

   while (!quit) {
      totalFrames++;

      while (SDL_PollEvent(&event)) {
         switch (event.type) {
         case SDL_KEYUP:
            /* Check for hotkeys. */
            curr_element = first_element;
            while (curr_element != NULL) {
               if (strncmp(curr_element->hotkey, "", 2) != 0) {
                  //printf("KEY: %d, %d\n", event.key.keysym.sym,
                  //       SDL_GetKeyFromName(curr_element->hotkey));
                  if (event.key.keysym.sym == SDL_GetKeyFromName(curr_element->hotkey)) {
                     curr_element->enabled = !curr_element->enabled;
                     if (strncmp(curr_element->special_name, "detect", 6) == 0) {
                        LOG_INFO("Changing detect status.");
                        detect_enabled = !detect_enabled;
                     }
                     LOG_INFO("Changing status.");
                  }
               }

               curr_element = curr_element->next;
            }

            /* Handle hotkeys for HUD switching */
hud_screen *screen = get_hud_manager()->screens;
while (screen != NULL) {
   if (screen->hotkey[0] != '\0') {
      if (event.key.keysym.sym == SDL_GetKeyFromName(screen->hotkey)) {
         /* Use default transition settings when using hotkeys */
         switch_to_hud(screen->name, get_hud_manager()->transition_type,
                      get_hud_manager()->transition_duration_ms);
         break;
      }
   }
   screen = screen->next;
}

            /* Process special keys. */
            switch (event.key.keysym.sym) {
            case SDLK_f:
               if (this_vod.output) {
                  LOG_WARNING("Unable to change window size while recording.");
               } else {
                  if (fullscreen) {
                     LOG_INFO("Switching to windowed mode.");
                     SDL_SetWindowFullscreen(window, 0);
                     SDL_ShowCursor(1);
                  } else {
                     LOG_INFO("Switching to fullscreen mode.");
                     SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                     SDL_ShowCursor(0);
                  }
                  fullscreen = !fullscreen;
               }
               break;
            case SDLK_r:
               if (!this_vod.output) {
                  this_vod.output = RECORD;
                  last_file_check = currTime;
                  LOG_INFO("Starting recording.");
               } else {
                  this_vod.output = 0;
                  last_size = last_last_size = -1;
                  LOG_INFO("Stopping recording.");
               }
               break;
            case SDLK_s:
               if (!this_vod.output) {
                  this_vod.output = STREAM;
                  last_file_check = currTime;
                  LOG_INFO("Starting streaming.");
               } else {
                  this_vod.output = 0;
                  last_size = last_last_size = -1;
                  LOG_INFO("Stopping streaming.");
               }
               break;
            case SDLK_t:
               if (!this_vod.output) {
                  this_vod.output = RECORD_STREAM;
                  last_file_check = currTime;
                  LOG_INFO("Starting recording and streaming.");
               } else {
                  this_vod.output = 0;
                  last_size = last_last_size = -1;
                  LOG_INFO("Stopping recording and streaming.");
               }
               break;
            case SDLK_LEFT:
               this_hds->stereo_offset -= 10;
               LOG_INFO("Stereo Offset: %d", this_hds->stereo_offset);
               break;
            case SDLK_RIGHT:
               this_hds->stereo_offset += 10;
               LOG_INFO("Stereo Offset: %d", this_hds->stereo_offset);
               break;
            case SDLK_ESCAPE:
            case SDLK_q:
               quit = 1;
               break;
            default:
               break;
            }
            break;
         case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                int newWidth = event.window.data1;
                int newHeight = event.window.data2;

                // Safely update the shared width and height
                pthread_mutex_lock(&windowSizeMutex);
                window_width = newWidth;
                window_height = newHeight;
                pthread_mutex_unlock(&windowSizeMutex);

                LOG_INFO("Window resized to: %dx%d", newWidth, newHeight);
            }
            break;
         case SDL_QUIT:
            quit = 1;
            break;
         default:
            break;
         }
      }

      SDL_RenderClear(renderer);

      currTime = SDL_GetTicks();

      thisPTime = SDL_GetPerformanceCounter();

      elapsed = (thisPTime - lastPTime) / (float)SDL_GetPerformanceFrequency();
      curr_fps = (int)(1.0f / elapsed);
      if (curr_fps == 0) {
         curr_fps = 60;
      }
      lastPTime = thisPTime;

      updateFrameRateTracker(&tracker, elapsed);

      if (tracker.elapsedTime > 1.0) {
         averageFrameRate = calculateAverageFrameRate(&tracker);
         tracker.elapsedTime = 0.0;
      }

#ifdef FPS_STATS
      printf("FPS Stats: %03d, avg: %03.0f, min: %03.0f, max: %03.0f\n",
             curr_fps, averageFrameRate, tracker.minFrameRate, tracker.maxFrameRate);
#endif

      /* Video Processing */
      pthread_mutex_lock(&v_mutex);
      if (video_posted) {
         if (detect_enabled) {
#if defined(OD_PROPER_WAIT) && defined(USE_CUDA)
            oddataL.pix_data = mapL[buffer_num].data;
            oddataL.eye = 0;
            cudaMemcpy(oddataL.detect_obj.d_image, oddataL.pix_data,
                       oddataL.detect_obj.l_width * oddataL.detect_obj.l_height * sizeof(uchar4), cudaMemcpyHostToDevice);
            detect_image(&oddataL.detect_obj, oddataL.pix_data, this_detect[oddataL.eye], MAX_DETECT);

            if (!single_cam) {
               oddataR.pix_data = mapR[buffer_num].data;
               oddataR.eye = 1;
               cudaMemcpy(oddataR.detect_obj.d_image, oddataR.pix_data,
                          oddataR.detect_obj.l_width * oddataR.detect_obj.l_height * sizeof(uchar4), cudaMemcpyHostToDevice);
               detect_image(&oddataR.detect_obj, oddataR.pix_data, this_detect[oddataR.eye], MAX_DETECT);
            }
#else
            if (single_cam) {
               if (oddataL.processed == 1) {
                  oddataL.pix_data = mapL[buffer_num].data;
                  oddataL.eye = 0;
                  oddataL.complete = 0;
                  oddataL.processed = 0;
                  pthread_create(&od_L_thread, NULL, object_detection_thread, &oddataL);
               }
            } else {
               if ((oddataL.processed == 1) && (oddataR.processed == 1)) {
                  oddataL.pix_data = mapL[buffer_num].data;
                  oddataL.eye = 0;
                  oddataL.complete = 0;
                  oddataL.processed = 0;
                  pthread_create(&od_L_thread, NULL, object_detection_thread, &oddataL);

                  oddataR.pix_data = mapR[buffer_num].data;
                  oddataR.eye = 1;
                  oddataR.complete = 0;
                  oddataR.processed = 0;
                  pthread_create(&od_R_thread, NULL, object_detection_thread, &oddataR);
               }
            }
#endif
         }

         SDL_UpdateTexture(textureL, NULL, mapL[buffer_num].data, this_hds->cam_input_width * 4);
         SDL_RenderCopy(renderer, textureL, &v_src_rect, &v_dst_rectL);

         if (single_cam) {
            SDL_RenderCopy(renderer, textureL, &v_src_rect, &v_dst_rectR);
         } else {
            SDL_UpdateTexture(textureR, NULL, mapR[buffer_num].data, this_hds->cam_input_width * 4);
            SDL_RenderCopy(renderer, textureR, &v_src_rect, &v_dst_rectR);
         }

#ifdef SNAPSHOT_NOOVERLAY
         if (snapshot) {
            void *snapshot_pixel = mapL[buffer_num].data;

            ImageProcessParams params = {
               .rgba_buffer = (unsigned char *) snapshot_pixel,
               .orig_width = this_hds->cam_input_width,
               .orig_height = this_hds->cam_input_height,
               .filename = snapshot_filename,
               .left_crop = this_hds->cam_crop_x,
               .top_crop = 0,
               .right_crop = this_hds->cam_crop_x,
               .bottom_crop = 0,
               .new_width = SNAPSHOT_WIDTH,
               .new_height = SNAPSHOT_HEIGHT,
               .format_params.quality = SNAPSHOT_QUALITY
            };

            /* FIXME: See how fast this happens. Do I need to spawn off a thread? */
            int result = process_and_save_image(&params);
            if (result != 0) {
               LOG_ERROR("Image processing failed with error code: %d", result);
               LOG_ERROR("\tfilename: %s, orig_width: %d, orig_height: %d",
                       params.filename, params.orig_width, params.orig_height);
            } else {
               LOG_INFO("Successfully created snapshot!");
               mqttViewingSnapshot(snapshot_filename);
            }

            snapshot = 0;
         }
#endif

#ifdef DISPLAY_TIMING
         last_ts_cap = (unsigned long) ts_cap[buffer_num].tv_sec * 1000000000 + ts_cap[buffer_num].tv_nsec;
#endif
      }
      pthread_mutex_unlock(&v_mutex);

      /* Element Processing */
      if (intro_element.enabled && !intro_finished) {
         play_intro(1, 0, &intro_finished);
      } else {
         render_hud_elements();

         renderArmor();

#ifdef DISPLAY_TIMING
         clock_gettime(CLOCK_REALTIME, &display_time);
         present_time = (unsigned long) display_time.tv_sec * 1000000000 + display_time.tv_nsec;
         ts_count++;
         ts_total += (present_time - last_ts_cap) / 1000000;
         LOG_INFO("Display latency: %lu ms, avg: %lu ms", (present_time - last_ts_cap) / 1000000,
                                                    ts_total / (unsigned long) ts_count);
         LOG_INFO("ts_total: %lu ms, ts_count: %u", ts_total, ts_count);
#endif

#ifndef SNAPSHOT_NOOVERLAY
         if (snapshot) {
            void *snapshot_pixel =
                malloc(this_hds->eye_output_width * 2 * RGB_OUT_SIZE * this_hds->eye_output_height);
            if (snapshot_pixel == NULL) {
               LOG_ERROR("Unable to malloc rgb frame 0.");
               return (2);
            }

            if (SDL_RenderReadPixels(renderer, NULL, PIXEL_FORMAT_OUT, snapshot_pixel,
                                     this_hds->eye_output_width * 2 * RGB_OUT_SIZE) != 0 ) {
               LOG_ERROR("SDL_RenderReadPixels() failed: %s", SDL_GetError());
            } else {
               ImageProcessParams params = {
                  .rgba_buffer = (unsigned char *) snapshot_pixel,
                  .orig_width = this_hds->eye_output_width * 2,
                  .orig_height = this_hds->eye_output_height,
                  .filename = snapshot_filename,
                  .left_crop = 0,
                  .top_crop = 0,
                  .right_crop = 1440,
                  .bottom_crop = 0,
                  .new_width = 512,
                  .new_height = 512,
                  .format_params.quality = 90
               };

               int result = process_and_save_image(&params);
               if (result != 0) {
                  LOG_ERROR("Image processing failed with error code: %d", result);
               } else {
                  LOG_INFO("Successfully created snapshot!");
                  mqttViewingSnapshot(snapshot_filename);
               }
            }

            free(snapshot_pixel);
            snapshot = 0;
         }
#endif

         if (this_vod.output) {
#ifdef ENCODE_TIMING
            Uint32 start = 0, stop = 0;
#endif
            pthread_mutex_lock(&windowSizeMutex);
            int local_window_width = window_width;
            int local_window_height = window_height;
            pthread_mutex_unlock(&windowSizeMutex);

            /* Is recording working? */
            if (((this_vod.output == RECORD) || (this_vod.output == RECORD_STREAM)) && 
                this_vod.started && ((currTime - last_file_check) > 5000)) {
               last_last_size = last_size;
               if (has_file_grown(this_vod.filename, &last_last_size)) {
                  if (!(active_alerts & ALERT_RECORDING)) {
                     LOG_ERROR("ERROR: %s: File size is not increasing. %ld ? %ld",
                            this_vod.filename, last_last_size, last_size);
                     active_alerts |= ALERT_RECORDING;
                     mqttTextToSpeech("There is potentially and error with recording.");
                  }
               } else {
                  active_alerts &= ~ALERT_RECORDING;
                  last_size = last_last_size;
               }
               last_file_check = currTime;
            }

            this_vod.rgb_out_pixels[!this_vod.buffer_num] =
                malloc(local_window_width * RGB_OUT_SIZE * local_window_height);
            if (this_vod.rgb_out_pixels[!this_vod.buffer_num] == NULL) {
               LOG_ERROR("Unable to malloc rgb frame 0.");
               return (2);
            }

#ifdef ENCODE_TIMING
            start = SDL_GetTicks();
#endif

            if (OpenGL_RenderReadPixelsAsync(renderer, NULL, PIXEL_FORMAT_OUT,
                                     this_vod.rgb_out_pixels[!this_vod.buffer_num],
                                     local_window_width * RGB_OUT_SIZE) != 0 ) {
               LOG_ERROR("OpenGL_RenderReadPixelsAsync() failed: %s", SDL_GetError());
#ifdef ENCODE_TIMING
            } else {
               stop = SDL_GetTicks();
               cur_time = stop - start;
               avg_time = ((avg_time * weight) + cur_time) / (weight + 1);
               weight++;
               if (cur_time > max_time)
                  max_time = cur_time;
               if ((cur_time < min_time) || (min_time == 0))
                  min_time = cur_time;
               printf("OpenGL_RenderReadPixelsAsync(): %0.2f ms, min: %d, max: %d. weight: %d\r",
                      avg_time, min_time, max_time, weight);
#endif
            }

            pthread_mutex_lock(&this_vod.p_mutex);
            if (this_vod.rgb_out_pixels[this_vod.buffer_num] != NULL) {
               free(this_vod.rgb_out_pixels[this_vod.buffer_num]);
            }
            this_vod.buffer_num = !this_vod.buffer_num;
            pthread_mutex_unlock(&this_vod.p_mutex);

            if (vid_out_thread == 0) {
               if (pthread_create(&vid_out_thread, NULL, video_next_thread, NULL) != 0) {
                  LOG_ERROR("Error creating video encoding thread.");
                  this_vod.output = 0;
               }
            }
         } else {
            if (active_alerts & ALERT_RECORDING) {
               active_alerts &= ~ALERT_RECORDING;
            }
         }

         SDL_RenderPresent(renderer);
      }
   }

   mqttTextToSpeech("Your hud is shutting down.");

   this_vod.output = 0;
   this_vod.buffer_num = 0;
   pthread_mutex_destroy(&this_vod.p_mutex);
   pthread_mutex_destroy(&v_mutex);
   pthread_mutex_destroy(&windowSizeMutex);

   /* Close audio threads. */
#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Waiting on audio threads to stop.");
#endif
   for (int i = 0; i < NUM_AUDIO_THREADS; i++) {
      pthread_join(thread_handles[i], NULL);
   }
#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Done.");

   LOG_INFO("Waiting on command processing thread to stop.");
#endif
   //pthread_join(command_proc_thread, NULL); // TODO: This is hanging.
   pthread_cancel(command_proc_thread);
#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Done.");

   LOG_INFO("Freeing elements.");
#endif
   /* Free elements. */
   free_elements(first_element);
   free_elements(this_as->armor_elements);
#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Done.");

   LOG_INFO("Freeing fonts.");
#endif
   /* Free fonts. */
   this_font = font_list;
   while (this_font != NULL) {
      TTF_CloseFont(this_font->ttf_font);
      this_font = this_font->next;
   }
   cleanup_hud_manager();
#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Done.");

   LOG_INFO("Waiting on MQTT disconnect.");
#endif
   mosquitto_disconnect(mosq);
#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Done.");

   LOG_INFO("Waiting on MQTT loop stop.");
#endif
   mosquitto_loop_stop(mosq, false);
#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Done.");

   LOG_INFO("Wainting on video processing to stop.");
#endif
   pthread_join(video_proc_thread, NULL);
#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Done.");
#endif
   if (vid_out_thread != 0) {
#ifdef DEBUG_SHUTDOWN
      LOG_INFO("Waiting on final video thread to stop.");
#endif
      pthread_join(vid_out_thread, NULL);
#ifdef DEBUG_SHUTDOWN
      LOG_INFO("Done.");
#endif
   }

#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Delete GL buffers.");
#endif
   glDeleteBuffers(2, g_pboIds);

#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Delete the GL context.");
#endif
   SDL_GL_DeleteContext(glContext);

#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Waiting on SDL renderer and window destruction.");
#endif
   SDL_DestroyRenderer(renderer);
   SDL_DestroyWindow(window);
#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Done.");
#endif

#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Waiting on SDL libraries to quit.");
#endif
   TTF_Quit();
   IMG_Quit();
   SDL_Quit();
#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Done.");
#endif

   if (detect_enabled)
   {
#ifdef DEBUG_SHUTDOWN
      LOG_INFO("Waiting for detection to clean up.");
#endif
      free_detect(&oddataL.detect_obj);
      free_detect(&oddataR.detect_obj);
#ifdef DEBUG_SHUTDOWN
      LOG_INFO("Done.");
#endif
   }

#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Waiting for other library clean up.");
#endif
   mosquitto_lib_cleanup();
   curl_global_cleanup();
#ifdef DEBUG_SHUTDOWN
   LOG_INFO("Done.");
#endif

   // Close the log file properly
   close_logging();

   return (EXIT_SUCCESS);
}
