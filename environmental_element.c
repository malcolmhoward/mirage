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

/*
 * Environmental UI Elements for HUD
 * Renders to a texture for stereo display
 * Optimized with SDL2_gfx
 */

#include "SDL2/SDL.h"
#include "SDL2/SDL2_gfxPrimitives.h"
#include <math.h>

#include "config_manager.h"
#include "config_parser.h"
#include "devices.h"
#include "logging.h"
#include "mirage.h"

// Texture for the environmental panel
static SDL_Texture *env_panel_texture = NULL;
static int env_texture_initialized = 0;
static Uint32 last_env_update = 0;
static const int UPDATE_INTERVAL_MS = 1000; // Update environmental panel every 1000ms

// Initialize environment panel texture
void init_environment_panel_texture(SDL_Renderer *renderer, int width, int height) {
   if (env_panel_texture != NULL) {
      SDL_DestroyTexture(env_panel_texture);
   }
    
   env_panel_texture = SDL_CreateTexture(
      renderer,
      SDL_PIXELFORMAT_RGBA32,
      SDL_TEXTUREACCESS_TARGET,
      width,
      height
   );
    
   if (env_panel_texture == NULL) {
      LOG_ERROR("Failed to create environmental panel texture: %s", SDL_GetError());
      return;
   }
    
   // Enable alpha blending for the texture
   SDL_SetTextureBlendMode(env_panel_texture, SDL_BLENDMODE_BLEND);
    
   env_texture_initialized = 1;
   LOG_INFO("Environmental panel texture initialized (%dx%d)", width, height);
}

// Function to render hex grid background pattern
void render_env_background(SDL_Renderer *renderer, int width, int height) {
   const int hex_size = 20;
   const int hex_spacing = 22;
   const int rows = height / (hex_spacing * 0.75) + 1;
   const int cols = width / hex_spacing + 1;
    
   for (int row = 0; row < rows; row++) {
      for (int col = 0; col < cols; col++) {
         int x = col * hex_spacing + ((row % 2) ? hex_spacing/2 : 0);
         int y = row * hex_spacing * 0.75;

         // Draw hexagon with SDL_gfx
         Sint16 vx[6], vy[6];
         for (int i = 0; i < 6; i++) {
            double angle = i * M_PI / 3;
            vx[i] = (Sint16)(x + hex_size * cos(angle));
            vy[i] = (Sint16)(y + hex_size * sin(angle));
         }

         // Draw hexagon with semi-transparent color
         polygonRGBA(renderer, vx, vy, 6, 0, 25, 40, 80);
      }
   }
}

// Function to render text for the environmental panel
void render_env_text(SDL_Renderer *renderer, const char *text, int x, int y,
                    TTF_Font *font, SDL_Color color, const char *halign) {
   if (!font || !text || strlen(text) == 0) {
      return;
   }

   // Create text surface
   SDL_Surface *text_surface = TTF_RenderText_Blended(font, text, color);
   if (!text_surface) {
      LOG_ERROR("Failed to render text surface: %s", TTF_GetError());
      return;
   }

   // Create texture from surface
   SDL_Texture *text_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
   if (!text_texture) {
      LOG_ERROR("Failed to create texture from text surface: %s", SDL_GetError());
      SDL_FreeSurface(text_surface);
      return;
   }

   // Set up destination rectangle
   SDL_Rect dst_rect;
   dst_rect.h = text_surface->h;
   dst_rect.w = text_surface->w;

   // Apply alignment
   if (strcmp(halign, "center") == 0) {
      dst_rect.x = x - (dst_rect.w / 2);
   } else if (strcmp(halign, "right") == 0) {
      dst_rect.x = x - dst_rect.w;
   } else {
      // Default to left alignment
      dst_rect.x = x;
   }
   dst_rect.y = y;

   // Render the texture
   SDL_RenderCopy(renderer, text_texture, NULL, &dst_rect);

   // Clean up
   SDL_DestroyTexture(text_texture);
   SDL_FreeSurface(text_surface);
}

// Function to render a horizontal bar gauge
void render_bar_gauge(SDL_Renderer *renderer, SDL_Rect bounds, 
                    double value, double max_value, 
                    Uint8 bg_r, Uint8 bg_g, Uint8 bg_b, Uint8 bg_a,
                    Uint8 fill_r, Uint8 fill_g, Uint8 fill_b, Uint8 fill_a) {
   // Draw background
   boxRGBA(renderer, bounds.x, bounds.y, 
           bounds.x + bounds.w - 1, bounds.y + bounds.h - 1, 
           bg_r, bg_g, bg_b, bg_a);
    
   // Calculate fill width
   int fill_width = (int)(bounds.w * (value / max_value));
   if (fill_width > bounds.w) fill_width = bounds.w;
    
   // Draw fill
   if (fill_width > 0) {
      boxRGBA(renderer, bounds.x, bounds.y, 
              bounds.x + fill_width - 1, bounds.y + bounds.h - 1, 
              fill_r, fill_g, fill_b, fill_a);
   }
    
   // Draw segment lines
   for (int i = 1; i < 10; i++) {
      int x = bounds.x + (bounds.w / 10) * i;
      vlineRGBA(renderer, x, bounds.y, bounds.y + bounds.h - 1, 
                255, 255, 255, 64);
   }
    
   // Draw border
   rectangleRGBA(renderer, bounds.x, bounds.y, 
                 bounds.x + bounds.w - 1, bounds.y + bounds.h - 1, 
                 255, 255, 255, 128);
}

// Render temperature visualization with animated "mercury"
void render_temp_visualization(SDL_Renderer *renderer, SDL_Rect bounds, 
                             double temp_c, double min_temp, double max_temp) {
   // Draw thermometer outline
   int bulb_radius = bounds.w / 2;
   int stem_width = bounds.w / 3;
   int stem_height = bounds.h - bulb_radius * 2;

   // Draw thermometer stem
   int stem_x = bounds.x + (bounds.w - stem_width) / 2;
   int stem_y = bounds.y;

   rectangleRGBA(renderer, stem_x, stem_y, 
                 stem_x + stem_width - 1, stem_y + stem_height - 1, 
                 0, 245, 252, 255);

   // Draw thermometer bulb
   int bulb_x = bounds.x + bounds.w / 2;
   int bulb_y = bounds.y + stem_height + bulb_radius;

   circleRGBA(renderer, bulb_x, bulb_y, bulb_radius, 0, 245, 252, 255);

   // Calculate fill height based on temperature
   double temp_percent = (temp_c - min_temp) / (max_temp - min_temp);
   temp_percent = temp_percent > 1.0 ? 1.0 : (temp_percent < 0.0 ? 0.0 : temp_percent);

   int fill_height = (int)(stem_height * temp_percent);

   // Determine color based on temperature (blue -> green -> yellow -> red)
   Uint8 r, g, b;
   if (temp_percent < 0.25) {
      // Blue to cyan
      r = 0;
      g = (Uint8)(255 * (temp_percent * 4));
      b = 255;
   } else if (temp_percent < 0.5) {
      // Cyan to green
      r = 0;
      g = 255;
      b = (Uint8)(255 * (1 - (temp_percent - 0.25) * 4));
   } else if (temp_percent < 0.75) {
      // Green to yellow
      r = (Uint8)(255 * ((temp_percent - 0.5) * 4));
      g = 255;
      b = 0;
   } else {
      // Yellow to red
      r = 255;
      g = (Uint8)(255 * (1 - (temp_percent - 0.75) * 4));
      b = 0;
   }

   // Draw temperature fill
   if (fill_height > 0) {
      boxRGBA(renderer, 
              stem_x + 1, stem_y + stem_height - fill_height, 
              stem_x + stem_width - 2, stem_y + stem_height - 1, 
              r, g, b, 255);
   }

   // Fill bulb
   filledCircleRGBA(renderer, bulb_x, bulb_y, bulb_radius - 1, r, g, b, 255);
}

// Function to render air quality hexagon pattern
void render_air_quality(SDL_Renderer *renderer, SDL_Rect bounds, double quality, int frame_num) {
   const int hex_size = 15;

   // Calculate center position
   int center_x = bounds.x + bounds.w / 2;
   int center_y = bounds.y + bounds.h / 2;

   // Map quality (0-100) to color
   Uint8 r, g, b;

   if (quality > 75) {
      // Good - Green
      r = 0; 
      g = 255; 
      b = 100;
   } else if (quality > 50) {
      // Moderate - Yellow
      r = 255; 
      g = 255; 
      b = 0;
   } else if (quality > 25) {
      // Poor - Orange
      r = 255; 
      g = 128; 
      b = 0;
   } else {
      // Very Poor - Red
      r = 255; 
      g = 0; 
      b = 0;
   }

   // Draw expanding hexagon pattern
   for (int ring = 0; ring < 4; ring++) {
      // Calculate alpha based on ring and quality
      Uint8 alpha = 255 - ring * 60;

      // Adjust size based on ring
      int current_size = hex_size + ring * 8;

      // Draw rotating hexagon (use frame_num for rotation)
      double rotation = (frame_num % 120) / 120.0 * M_PI / 3;
      Sint16 vx[6], vy[6];

      for (int i = 0; i < 6; i++) {
         double angle = i * M_PI / 3 + rotation;
         vx[i] = center_x + current_size * cos(angle);
         vy[i] = center_y + current_size * sin(angle);
      }

      polygonRGBA(renderer, vx, vy, 6, r, g, b, alpha);

      // Draw additional pulsing inner circle for good/moderate air quality
      if (quality > 50 && ring == 0) {
         double pulse_size = 1.0 + 0.1 * sin(frame_num * 0.1);
         filledCircleRGBA(renderer, center_x, center_y, 
                          current_size/3 * pulse_size, r, g, b, alpha/2);
      }

      // Calculate how many particles to show based on quality
      int particle_count = (int)(quality / 10) - ring * 2;
      if (particle_count < 0) particle_count = 0;

      // Use frame_num to animate particles
      unsigned int seed = frame_num + ring * 100;

      // Draw particles emanating from center
      for (int i = 0; i < particle_count; i++) {
         // Deterministic "random" based on frame, ring, and particle index
         seed = (seed * 1103515245 + 12345) & 0x7fffffff;
         double angle = (seed % 360) * (M_PI / 180.0);

         seed = (seed * 1103515245 + 12345) & 0x7fffffff;
         double dist = (seed % 100) / 100.0 * current_size;

         int px = center_x + dist * cos(angle);
         int py = center_y + dist * sin(angle);

         // Draw particles with size based on distance from center
         int particle_size = 1 + (current_size - dist) / 10;
         filledCircleRGBA(renderer, px, py, particle_size, r, g, b, alpha);
      }
   }
}

// Function to render CO2 visualization
void render_co2_level(SDL_Renderer *renderer, SDL_Rect bounds, double co2_ppm, int frame_num) {
   // Set color based on CO2 levels
   Uint8 r, g, b;

   if (co2_ppm < 800) {
      // Good - Green
      r = 0; 
      g = 255; 
      b = 100;
   } else if (co2_ppm < 1200) {
      // Acceptable - Yellow
      r = 255; 
      g = 255; 
      b = 0;
   } else if (co2_ppm < 2000) {
      // Poor - Orange
      r = 255; 
      g = 128; 
      b = 0;
   } else {
      // Dangerous - Red
      r = 255; 
      g = 0; 
      b = 0;
   }

   // Calculate center position
   int center_x = bounds.x + bounds.w / 2;
   int center_y = bounds.y + bounds.h / 2;

   // Draw C atom (center)
   filledCircleRGBA(renderer, center_x, center_y, 15, 80, 80, 80, 255);

   // Draw O atoms (outer)
   filledCircleRGBA(renderer, center_x - 25, center_y, 12, r, g, b, 255);
   filledCircleRGBA(renderer, center_x + 25, center_y, 12, r, g, b, 255);

   // Draw bonds
   // Left bond
   thickLineRGBA(renderer, center_x - 10, center_y, center_x - 15, center_y, 
                 3, 255, 255, 255, 180);
   // Right bond
   thickLineRGBA(renderer, center_x + 10, center_y, center_x + 15, center_y, 
                 3, 255, 255, 255, 180);

   // Add pulsating effect based on CO2 level and frame number
   double pulse = (frame_num % 60) / 60.0 * 2 * M_PI;

   // Calculate pulse size based on CO2 level
   double pulse_size = 1.0 + sin(pulse) * 0.2 * (co2_ppm / 1000.0);

   // Draw pulsating circle
   circleRGBA(renderer, center_x, center_y, (int)(40 * pulse_size), r, g, b, 100);

   // Add additional visual cue for dangerous levels
   if (co2_ppm >= 2000) {
      int warning_blink = (frame_num / 15) % 2;
      if (warning_blink) {
         aacircleRGBA(renderer, center_x, center_y, 45, 255, 0, 0, 200);
      }
   }
}

// Function to render circular gauge
void render_circular_gauge(SDL_Renderer *renderer, int x, int y, int radius, 
                          double value, double max_value, 
                          Uint8 bg_r, Uint8 bg_g, Uint8 bg_b, Uint8 bg_a,
                          Uint8 fill_r, Uint8 fill_g, Uint8 fill_b, Uint8 fill_a) {
   const int segments = 36;
   double angle_step = 2.0 * M_PI / segments;
   double fill_percent = value / max_value;
   fill_percent = fill_percent > 1.0 ? 1.0 : (fill_percent < 0.0 ? 0.0 : fill_percent);
   int fill_segments = (int)(segments * fill_percent);

   // Draw background circle
   aacircleRGBA(renderer, x, y, radius, bg_r, bg_g, bg_b, bg_a);

   // Draw segments
   for (int i = 0; i < segments; i++) {
      double angle1 = i * angle_step - M_PI/2; // Start from top
      double angle2 = (i + 1) * angle_step - M_PI/2;

      int x1 = x + radius * cos(angle1);
      int y1 = y + radius * sin(angle1);
      int x2 = x + radius * cos(angle2);
      int y2 = y + radius * sin(angle2);

      // Use fill color for filled segments, background color for others
      if (i < fill_segments) {
         thickLineRGBA(renderer, x1, y1, x2, y2, 2, fill_r, fill_g, fill_b, fill_a);
         // Draw line from center
         lineRGBA(renderer, x, y, x1, y1, fill_r, fill_g, fill_b, fill_a);
      } else {
         lineRGBA(renderer, x1, y1, x2, y2, bg_r, bg_g, bg_b, bg_a);
      }
   }

   // Draw central point
   filledCircleRGBA(renderer, x, y, 3, fill_r, fill_g, fill_b, fill_a);
}

// Function to render the environmental panel to a texture
void update_environment_panel(element *curr_element, int frame_num) {
   // Add these variables at the beginning of the function
   TTF_Font *title_font = NULL;
   TTF_Font *label_font = NULL;
   TTF_Font *value_font = NULL;
   char text_buffer[64];
   SDL_Color text_color = curr_element->font_color;

   SDL_Renderer *renderer = get_sdl_renderer();
   enviro *env_data = get_enviro_dev();

   // Skip update if not time yet (to avoid too frequent updates)
   Uint32 current_time = SDL_GetTicks();
   if (current_time - last_env_update < UPDATE_INTERVAL_MS && env_texture_initialized) {
      return;
   }

   // Initialize texture if needed
   if (!env_texture_initialized) {
      init_environment_panel_texture(renderer, curr_element->width, curr_element->height);
      if (!env_texture_initialized) {
         return;
      }
   }

   // Set render target to our texture
   SDL_Texture *previous_target = SDL_GetRenderTarget(renderer);
   SDL_SetRenderTarget(renderer, env_panel_texture);

   // Clear the texture with transparent background
   SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
   SDL_RenderClear(renderer);

   // After initializing the texture
   // Load fonts
   title_font = get_local_font(curr_element->font, 24);
   label_font = get_local_font(curr_element->font, 18);
   value_font = get_local_font(curr_element->font, 16);

   // If font loading failed, log error but continue with visual elements
   if (!title_font || !label_font || !value_font) {
      LOG_WARNING("Failed to load one or more fonts for environmental panel");
   }

   // Panel dimensions
   int panel_width = curr_element->width;
   int panel_height = curr_element->height;

   // Draw hexagon background pattern
   render_env_background(renderer, panel_width, panel_height);

   // Draw panel border with a stylish glow effect
   // Outer glow
   rectangleRGBA(renderer, 1, 1, panel_width-2, panel_height-2, 0, 245, 252, 100);
   // Main border
   rectangleRGBA(renderer, 0, 0, panel_width-1, panel_height-1, 0, 245, 252, 255);

   // Panel title
   if (title_font) {
      render_env_text(renderer, "ENVIRONMENTAL ANALYSIS",
                      panel_width / 2, 20, title_font, text_color, "center");
   }

   // === Temperature Visualization ===
   SDL_Rect temp_bounds = {20, 60, 40, 200};
   render_temp_visualization(renderer, temp_bounds, env_data->temp, 0, 50);

   // Temperature value
   if (value_font) {
      snprintf(text_buffer, sizeof(text_buffer), "%.1f C", env_data->temp);
      render_env_text(renderer, text_buffer,
                      temp_bounds.x + temp_bounds.w / 2,
                      temp_bounds.y + temp_bounds.h + 10,
                      value_font, text_color, "center");
   }

   // === Air Quality Visualization ===
   SDL_Rect air_bounds = {100, 80, 150, 150};
   render_air_quality(renderer, air_bounds, env_data->air_quality, frame_num);

   // Air quality label
   if (label_font) {
      render_env_text(renderer, "AIR QUALITY",
                      air_bounds.x + air_bounds.w / 2,
                      air_bounds.y - 25,
                      label_font, text_color, "center");
   }
   if (value_font) {
      snprintf(text_buffer, sizeof(text_buffer), "%.0f%%", env_data->air_quality);
      render_env_text(renderer, text_buffer,
                      air_bounds.x + air_bounds.w / 2,
                      air_bounds.y + air_bounds.h + 10,
                      value_font, text_color, "center");
   }

   // === CO2 Visualization ===
   SDL_Rect co2_bounds = {300, 80, 80, 80};
   render_co2_level(renderer, co2_bounds, env_data->co2_ppm, frame_num);

   // CO2 label
   if (label_font) {
      render_env_text(renderer, "CO2",
                      co2_bounds.x + co2_bounds.w / 2,
                      co2_bounds.y - 25,
                      label_font, text_color, "center");
   }
   if (value_font) {
      snprintf(text_buffer, sizeof(text_buffer), "%.0f PPM", env_data->co2_ppm);
      render_env_text(renderer, text_buffer,
                      co2_bounds.x + co2_bounds.w / 2,
                      co2_bounds.y + co2_bounds.h + 10,
                      value_font, text_color, "center");
   }

   // === Humidity Gauge ===
   SDL_Rect humidity_bounds = {420, 80, 150, 30};
   render_bar_gauge(renderer, humidity_bounds, 
                    env_data->humidity, 100, 
                    0, 32, 64, 255,  // Background color
                    0, 128, 255, 255); // Fill color

   // Humidity label
   if (label_font) {
      render_env_text(renderer, "HUMIDITY",
                      humidity_bounds.x,
                      humidity_bounds.y - 25,
                      label_font, text_color, "left");
   }
   if (value_font) {
      snprintf(text_buffer, sizeof(text_buffer), "%.0f%%", env_data->humidity);
      render_env_text(renderer, text_buffer,
                      humidity_bounds.x + humidity_bounds.w + 10,
                      humidity_bounds.y + humidity_bounds.h / 2 - 8,
                      value_font, text_color, "left");
   }

   // === VOC Gauge ===
   SDL_Rect voc_bounds = {420, 150, 150, 30};
   render_bar_gauge(renderer, voc_bounds, 
                    env_data->tvoc_ppb, 1000, 
                    32, 32, 32, 255,  // Background color
                    128, 255, 32, 255); // Fill color

   // VOC label
   if (label_font) {
      render_env_text(renderer, "VOC",
                      voc_bounds.x,
                      voc_bounds.y - 25,
                      label_font, text_color, "left");
   }
   if (value_font) {
      snprintf(text_buffer, sizeof(text_buffer), "%.0f PPB", env_data->tvoc_ppb);
      render_env_text(renderer, text_buffer,
                      voc_bounds.x + voc_bounds.w + 10,
                      voc_bounds.y + voc_bounds.h / 2 - 8,
                      value_font, text_color, "left");
   }

   // === Heat Index Circular Gauge ===
   render_circular_gauge(renderer, 
                         500, 250, 50, 
                         env_data->heat_index_c, 50, 
                         32, 32, 32, 255,  // Background color
                         255, 64, 64, 255); // Fill color

   // Heat index label placeholder
   if (label_font) {
      render_env_text(renderer, "HEAT INDEX",
                      500,
                      320,
                      label_font, text_color, "center");
   }
   if (value_font) {
      snprintf(text_buffer, sizeof(text_buffer), "%.1f C", env_data->heat_index_c);
      render_env_text(renderer, text_buffer,
                      500,
                      250 - 20,
                      value_font, text_color, "center");
   }

   // Add decorative elements
   // Tech corner brackets
   // Top-left corner
   thickLineRGBA(renderer, 5, 5, 25, 5, 2, 0, 245, 252, 255);
   thickLineRGBA(renderer, 5, 5, 5, 25, 2, 0, 245, 252, 255);

   // Top-right corner
   thickLineRGBA(renderer, panel_width-25, 5, panel_width-5, 5, 2, 0, 245, 252, 255);
   thickLineRGBA(renderer, panel_width-5, 5, panel_width-5, 25, 2, 0, 245, 252, 255);

   // Bottom-left corner
   thickLineRGBA(renderer, 5, panel_height-5, 25, panel_height-5, 2, 0, 245, 252, 255);
   thickLineRGBA(renderer, 5, panel_height-25, 5, panel_height-5, 2, 0, 245, 252, 255);

   // Bottom-right corner
   thickLineRGBA(renderer, panel_width-25, panel_height-5, panel_width-5, panel_height-5, 2, 0, 245, 252, 255);
   thickLineRGBA(renderer, panel_width-5, panel_height-25, panel_width-5, panel_height-5, 2, 0, 245, 252, 255);

   // Reset render target to previous
   SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Black, opaque
   SDL_SetRenderTarget(renderer, previous_target);

   // Update timestamp
   last_env_update = current_time;
}

// Function to render environmental panel element
void render_environmental_panel_element(element *curr_element) {
   if (!curr_element->enabled) {
      return;
   }

   SDL_Rect dst_rect_l, dst_rect_r;
   hud_display_settings *this_hds = get_hud_display_settings();
   motion *this_motion = get_motion_dev();

   // Calculate frame number (for animations)
   Uint32 currTime = SDL_GetTicks();
   int frame_num = currTime / 50; // 20 FPS animation rate

   // Update texture (if needed)
   update_environment_panel(curr_element, frame_num);

   if (!env_texture_initialized) {
      return; // Skip rendering if texture isn't ready
   }

   // Set up destination rectangles
   dst_rect_l.x = dst_rect_r.x = curr_element->dst_rect.x;
   dst_rect_l.y = dst_rect_r.y = curr_element->dst_rect.y;
   dst_rect_l.w = dst_rect_r.w = curr_element->width;
   dst_rect_l.h = dst_rect_r.h = curr_element->height;

   // Apply fixed/stereo offset
   if (!curr_element->fixed) {
      dst_rect_l.x -= this_hds->stereo_offset;
      dst_rect_r.x += this_hds->stereo_offset;
   }

   // Render the element using renderStereo
   if (env_panel_texture != NULL) {
      if (curr_element->angle == ANGLE_OPPOSITE_ROLL) {
         renderStereo(env_panel_texture, NULL, &dst_rect_l, &dst_rect_r, -1.0 * this_motion->roll);
      } else if (curr_element->angle == ANGLE_ROLL) {
         renderStereo(env_panel_texture, NULL, &dst_rect_l, &dst_rect_r, this_motion->roll);
      } else {
         renderStereo(env_panel_texture, NULL, &dst_rect_l, &dst_rect_r, curr_element->angle);
      }
   }
}

// Cleanup function for the environmental panel
void cleanup_environmental_panel() {
   if (env_panel_texture != NULL) {
      SDL_DestroyTexture(env_panel_texture);
      env_panel_texture = NULL;
   }
   env_texture_initialized = 0;
}
