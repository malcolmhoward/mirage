/*
 * cuda_color_correction.cu - CUDA Color Correction Implementation
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
 * High-performance color correction for NoIR cameras using CUDA
 */

#ifdef USE_CUDA

#include "cuda_color_correction.h"
#include <cuda_runtime.h>
#include <stdio.h>

/* CUDA error checking macro */
#define CUDA_CHECK(call) do { \
   cudaError_t error = call; \
   if (error != cudaSuccess) { \
      fprintf(stderr, "CUDA error at %s:%d - %s\n", \
              __FILE__, __LINE__, cudaGetErrorString(error)); \
      return -1; \
   } \
} while(0)

/* Block dimensions for kernel launch */
#define BLOCK_WIDTH  16
#define BLOCK_HEIGHT 16

/* Device constant memory for color matrix (faster access) */
__constant__ float d_colorMatrix[3][3];
__constant__ float d_colorOffset[3];

/**
 * CUDA kernel for color correction
 * Applies a 3x3 color matrix transformation to RGBA image
 */
__global__ void colorCorrectionKernel(
   unsigned char* input,
   unsigned char* output,
   int width,
   int height)
{
   int x = blockIdx.x * blockDim.x + threadIdx.x;
   int y = blockIdx.y * blockDim.y + threadIdx.y;
   
   if (x >= width || y >= height) return;
   
   int idx = (y * width + x) * 4; // RGBA format
   
   /* Read input pixel values */
   float r = input[idx + 0];
   float g = input[idx + 1];
   float b = input[idx + 2];
   unsigned char a = input[idx + 3];
   
   /* Apply color matrix transformation */
   float r_new = d_colorMatrix[0][0] * r + 
                 d_colorMatrix[0][1] * g + 
                 d_colorMatrix[0][2] * b + 
                 d_colorOffset[0];
                 
   float g_new = d_colorMatrix[1][0] * r + 
                 d_colorMatrix[1][1] * g + 
                 d_colorMatrix[1][2] * b + 
                 d_colorOffset[1];
                 
   float b_new = d_colorMatrix[2][0] * r + 
                 d_colorMatrix[2][1] * g + 
                 d_colorMatrix[2][2] * b + 
                 d_colorOffset[2];
   
   /* Clamp values to valid range [0, 255] */
   r_new = fminf(fmaxf(r_new, 0.0f), 255.0f);
   g_new = fminf(fmaxf(g_new, 0.0f), 255.0f);
   b_new = fminf(fmaxf(b_new, 0.0f), 255.0f);
   
   /* Write output pixel */
   output[idx + 0] = (unsigned char)r_new;
   output[idx + 1] = (unsigned char)g_new;
   output[idx + 2] = (unsigned char)b_new;
   output[idx + 3] = a; // Preserve alpha
}

/**
 * Optimized kernel using shared memory for coalesced access
 */
__global__ void colorCorrectionKernelOptimized(
   unsigned char* __restrict__ input,
   unsigned char* __restrict__ output,
   int width,
   int height)
{
   __shared__ uchar4 tile[BLOCK_HEIGHT][BLOCK_WIDTH];
   
   int x = blockIdx.x * blockDim.x + threadIdx.x;
   int y = blockIdx.y * blockDim.y + threadIdx.y;
   
   /* Load data into shared memory */
   if (x < width && y < height) {
      int idx = y * width + x;
      tile[threadIdx.y][threadIdx.x] = ((uchar4*)input)[idx];
   }
   __syncthreads();
   
   if (x >= width || y >= height) return;
   
   /* Process from shared memory */
   uchar4 pixel = tile[threadIdx.y][threadIdx.x];
   float r = pixel.x;
   float g = pixel.y;
   float b = pixel.z;
   
   /* Apply transformation using constant memory matrices */
   float r_new = d_colorMatrix[0][0] * r + 
                 d_colorMatrix[0][1] * g + 
                 d_colorMatrix[0][2] * b + 
                 d_colorOffset[0];
                 
   float g_new = d_colorMatrix[1][0] * r + 
                 d_colorMatrix[1][1] * g + 
                 d_colorMatrix[1][2] * b + 
                 d_colorOffset[1];
                 
   float b_new = d_colorMatrix[2][0] * r + 
                 d_colorMatrix[2][1] * g + 
                 d_colorMatrix[2][2] * b + 
                 d_colorOffset[2];
   
   /* Write output with clamping */
   int idx = y * width + x;
   ((uchar4*)output)[idx] = make_uchar4(
      (unsigned char)fminf(fmaxf(r_new, 0.0f), 255.0f),
      (unsigned char)fminf(fmaxf(g_new, 0.0f), 255.0f),
      (unsigned char)fminf(fmaxf(b_new, 0.0f), 255.0f),
      pixel.w
   );
}

/* Initialize CUDA color correction module */
int cuda_color_init(void)
{
   /* Check for CUDA device */
   int deviceCount;
   CUDA_CHECK(cudaGetDeviceCount(&deviceCount));
   
   if (deviceCount == 0) {
      fprintf(stderr, "No CUDA devices found\n");
      return -1;
   }
   
   /* Set device and print info */
   CUDA_CHECK(cudaSetDevice(0));
   
   cudaDeviceProp prop;
   CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));
   printf("CUDA Color Correction initialized on %s\n", prop.name);
   
   return 0;
}

/* Cleanup CUDA resources */
void cuda_color_cleanup(void)
{
   cudaDeviceReset();
}

/* Apply color correction to device memory */
int cuda_apply_color_correction(
   unsigned char* d_input,
   unsigned char* d_output,
   int width,
   int height,
   const cuda_color_matrix_t* matrix)
{
   /* Copy matrix to constant memory */
   CUDA_CHECK(cudaMemcpyToSymbol(d_colorMatrix, matrix->m, 
                                  sizeof(float) * 9));
   CUDA_CHECK(cudaMemcpyToSymbol(d_colorOffset, matrix->off, 
                                  sizeof(float) * 3));
   
   /* Configure kernel launch parameters */
   dim3 blockSize(BLOCK_WIDTH, BLOCK_HEIGHT);
   dim3 gridSize((width + blockSize.x - 1) / blockSize.x,
                 (height + blockSize.y - 1) / blockSize.y);
   
   /* Launch optimized kernel */
   colorCorrectionKernelOptimized<<<gridSize, blockSize>>>(
      d_input, d_output, width, height);
   
   /* Check for kernel errors */
   CUDA_CHECK(cudaGetLastError());
   CUDA_CHECK(cudaDeviceSynchronize());
   
   return 0;
}

/* Apply color correction to host memory */
int cuda_apply_color_correction_host(
   unsigned char* h_input,
   unsigned char* h_output,
   int width,
   int height,
   const cuda_color_matrix_t* matrix)
{
   size_t imageSize = width * height * 4 * sizeof(unsigned char);
   unsigned char *d_input, *d_output;
   
   /* Allocate device memory */
   CUDA_CHECK(cudaMalloc(&d_input, imageSize));
   CUDA_CHECK(cudaMalloc(&d_output, imageSize));
   
   /* Copy input to device */
   CUDA_CHECK(cudaMemcpy(d_input, h_input, imageSize, 
                         cudaMemcpyHostToDevice));
   
   /* Apply color correction */
   int result = cuda_apply_color_correction(d_input, d_output, 
                                            width, height, matrix);
   
   if (result == 0) {
      /* Copy result back to host */
      CUDA_CHECK(cudaMemcpy(h_output, d_output, imageSize, 
                            cudaMemcpyDeviceToHost));
   }
   
   /* Free device memory */
   cudaFree(d_input);
   cudaFree(d_output);
   
   return result;
}

/* Optimized version using pre-allocated device memory */
int cuda_apply_color_correction_optimized(
   unsigned char* h_input,
   unsigned char* h_output,
   unsigned char* d_buffer,
   int width,
   int height,
   const cuda_color_matrix_t* matrix,
   cudaStream_t stream)
{
   size_t imageSize = width * height * 4 * sizeof(unsigned char);
   
   /* Copy matrix to constant memory */
   CUDA_CHECK(cudaMemcpyToSymbolAsync(d_colorMatrix, matrix->m, 
                                       sizeof(float) * 9, 0,
                                       cudaMemcpyHostToDevice, stream));
   CUDA_CHECK(cudaMemcpyToSymbolAsync(d_colorOffset, matrix->off, 
                                       sizeof(float) * 3, 0,
                                       cudaMemcpyHostToDevice, stream));
   
   /* Copy input to device using the pre-allocated buffer */
   CUDA_CHECK(cudaMemcpyAsync(d_buffer, h_input, imageSize,
                               cudaMemcpyHostToDevice, stream));
   
   /* Configure kernel launch parameters */
   dim3 blockSize(BLOCK_WIDTH, BLOCK_HEIGHT);
   dim3 gridSize((width + blockSize.x - 1) / blockSize.x,
                 (height + blockSize.y - 1) / blockSize.y);
   
   /* Launch kernel for in-place processing */
   colorCorrectionKernelOptimized<<<gridSize, blockSize, 0, stream>>>(
      d_buffer, d_buffer, width, height);
   
   /* Check for kernel errors */
   CUDA_CHECK(cudaGetLastError());
   
   /* Copy result back to host */
   CUDA_CHECK(cudaMemcpyAsync(h_output, d_buffer, imageSize,
                               cudaMemcpyDeviceToHost, stream));
   
   /* Synchronize stream */
   CUDA_CHECK(cudaStreamSynchronize(stream));
   
   return 0;
}

#endif /* USE_CUDA */

