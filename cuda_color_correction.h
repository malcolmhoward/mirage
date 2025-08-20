/*
 * cuda_color_correction.h - CUDA Color Correction for NoIR Cameras
 *
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
 * This module provides CUDA-accelerated color correction specifically
 * designed to compensate for the pink/purple hue in NoIR camera images.
 */

#ifndef CUDA_COLOR_CORRECTION_H
#define CUDA_COLOR_CORRECTION_H

#ifdef USE_CUDA

#include <cuda_runtime.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Color correction matrix structure */
typedef struct {
   float m[3][3];    /* 3x3 transformation matrix */
   float off[3];     /* RGB offsets */
   int shift;        /* Bit shift for fixed-point math (if needed) */
} cuda_color_matrix_t;

/* Optimized color matrix for NoIR camera daylight correction
 * Based on research and testing, this matrix reduces the pink hue
 * while preserving natural colors */
static const cuda_color_matrix_t CCM_NOIR_DAYLIGHT = {
   /* R' = 0.85R + 0.10G + 0.05B
    * G' = 0.00R + 0.95G + 0.05B  
    * B' = 0.00R + 0.15G + 0.85B */
   .m = {{0.85f, 0.10f, 0.05f},
         {0.00f, 0.95f, 0.05f},
         {0.00f, 0.15f, 0.85f}},
   .off = {0.0f, 0.0f, 0.0f},
   .shift = 0
};

/* Alternative matrix based on tuned software filter (converted to float) */
static const cuda_color_matrix_t CCM_NOIR_DAYLIGHT_ALT = {
   .m = {{0.921875f, 0.0625f, 0.0f},    /* 59/64, 4/64, 0 */
         {0.0f,      1.0f,     0.0f},
         {0.0f,      0.046875f, 0.96875f}}, /* 0, 3/64, 62/64 */
   .off = {0.0f, 0.0f, 0.0f},
   .shift = 0
};

/* TEST MATRICES - For verifying the filter is working */

/* Makes everything blue-tinted for testing */
static const cuda_color_matrix_t CCM_TEST_BLUE = {
   .m = {{0.2f, 0.0f, 0.0f},    /* R' = 0.2*R (reduce red) */
         {0.0f, 0.3f, 0.0f},    /* G' = 0.3*G (reduce green) */
         {0.0f, 0.0f, 1.5f}},   /* B' = 1.5*B (amplify blue) */
   .off = {0.0f, 0.0f, 50.0f},  /* Add blue offset */
   .shift = 0
};

/* Inverts colors for dramatic test */
static const cuda_color_matrix_t CCM_TEST_INVERT = {
   .m = {{-1.0f, 0.0f, 0.0f},
         {0.0f, -1.0f, 0.0f},
         {0.0f, 0.0f, -1.0f}},
   .off = {255.0f, 255.0f, 255.0f},
   .shift = 0
};

/* Sepia tone effect for testing */
static const cuda_color_matrix_t CCM_TEST_SEPIA = {
   .m = {{0.393f, 0.769f, 0.189f},
         {0.349f, 0.686f, 0.168f},
         {0.272f, 0.534f, 0.131f}},
   .off = {0.0f, 0.0f, 0.0f},
   .shift = 0
};

/* Function prototypes */

/**
 * @brief Initialize CUDA color correction module
 *
 * @return 0 on success, non-zero on failure
 */
int cuda_color_init(void);

/**
 * @brief Cleanup CUDA color correction resources
 */
void cuda_color_cleanup(void);

/**
 * @brief Apply color correction to RGBA image
 *
 * @param d_input   Device pointer to input RGBA image
 * @param d_output  Device pointer to output RGBA image (can be same as input)
 * @param width     Image width
 * @param height    Image height
 * @param matrix    Color correction matrix to apply
 * @return 0 on success, non-zero on failure
 */
int cuda_apply_color_correction(
   unsigned char* d_input,
   unsigned char* d_output,
   int width,
   int height,
   const cuda_color_matrix_t* matrix
);

/**
 * @brief Apply color correction to host RGBA image (handles memory transfers)
 *
 * @param h_input   Host pointer to input RGBA image
 * @param h_output  Host pointer to output RGBA image
 * @param width     Image width
 * @param height    Image height
 * @param matrix    Color correction matrix to apply
 * @return 0 on success, non-zero on failure
 */
int cuda_apply_color_correction_host(
   unsigned char* h_input,
   unsigned char* h_output,
   int width,
   int height,
   const cuda_color_matrix_t* matrix
);

/**
 * @brief Apply color correction using pre-allocated device memory (optimized)
 * @param h_input    Host pointer to input RGBA image
 * @param h_output   Host pointer to output RGBA image  
 * @param d_buffer   Pre-allocated device buffer (must be at least width*height*4 bytes)
 * @param width      Image width
 * @param height     Image height
 * @param matrix     Color correction matrix to apply
 * @param stream     CUDA stream for async operations (can be 0 for default)
 * @return 0 on success, non-zero on failure
 */
int cuda_apply_color_correction_optimized(
   unsigned char* h_input,
   unsigned char* h_output,
   unsigned char* d_buffer,
   int width,
   int height,
   const cuda_color_matrix_t* matrix,
   cudaStream_t stream
);

#ifdef __cplusplus
}
#endif

#endif /* USE_CUDA */

#endif /* CUDA_COLOR_CORRECTION_H */

