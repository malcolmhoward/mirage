cmake_minimum_required(VERSION 3.10)

set(PROJECT_NAME mirage)
project(${PROJECT_NAME})

# Version and Git SHA
set(VERSION_NUMBER "1.0.0")
execute_process(
   COMMAND git rev-parse --short HEAD
   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
   OUTPUT_VARIABLE GIT_SHA
   OUTPUT_STRIP_TRAILING_WHITESPACE
   ERROR_QUIET
   RESULT_VARIABLE GIT_SHA_RESULT
)
if(NOT GIT_SHA_RESULT EQUAL 0)
   set(GIT_SHA "unknown")
endif()
add_definitions(-DVERSION_NUMBER=\"${VERSION_NUMBER}\")
add_definitions(-DGIT_SHA=\"${GIT_SHA}\")

# Platform selection
set(PLATFORM "AUTO" CACHE STRING "Choose platform: AUTO, JETSON, RPI")
set_property(CACHE PLATFORM PROPERTY STRINGS AUTO JETSON RPI)

# Platform detection
if(PLATFORM STREQUAL "AUTO")
    # Check if it's a Jetson board first
    if(EXISTS "/etc/nv_tegra_release")
        set(PLATFORM "JETSON")
        file(READ "/etc/nv_tegra_release" JETSON_MODEL)
        message(STATUS "Detected Jetson: ${JETSON_MODEL}")
    # Then check if it's a Raspberry Pi (both ARM and ARM64)
    elseif(EXISTS "/sys/firmware/devicetree/base/model")
        file(READ "/sys/firmware/devicetree/base/model" RPI_MODEL)
        if(RPI_MODEL MATCHES "Raspberry Pi")
            set(PLATFORM "RPI")
            message(STATUS "Detected RPi: ${RPI_MODEL}")
        endif()
    # Default platform detection based on processor architecture
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        message(STATUS "Detected generic ARM64 platform")
        # Default platform for ARM64 if needed
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "armv")
        message(STATUS "Detected generic ARM platform")
        # Default platform for ARM if needed
    endif()
endif()

# Options - set CUDA default based on platform
if(PLATFORM STREQUAL "JETSON")
   option(USE_CUDA "Enable CUDA support" ON)
else()
   option(USE_CUDA "Enable CUDA support" OFF)
endif()

# Options
option(USE_JETSON_INFERENCE "Enable Jetson Inference support" OFF)
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose build type: Debug or Release")

# Enable CUDA language support if requested
if(USE_CUDA)
   enable_language(CUDA)
   # Set CMake policy for modern CUDA support
   if(POLICY CMP0146)
      cmake_policy(SET CMP0146 NEW)
   endif()
endif()

# Platform definitions
add_definitions(-DPLATFORM_${PLATFORM})
message(STATUS "Building for platform: ${PLATFORM}")

# Source files
set(SOURCE_FILES
   armor.c
   audio.c
   command_processing.c
   config_manager.c
   config_parser.c
   curl_download.c
   devices.c
   element_renderer.c
   frame_rate_tracker.c
   hud_manager.c
   image_utils.c
   logging.c
   mirage.c
   mosquitto_comms.c
   recording.c
   screenshot.c
   system_metrics.c
   utils.c
)

# Add CUDA source files if enabled
if(USE_CUDA)
   list(APPEND SOURCE_FILES cuda_color_correction.cu)
endif()

# Add Jetson Inference files if enabled
if(USE_JETSON_INFERENCE)
   list(APPEND SOURCE_FILES detect.cpp)
   include_directories(/usr/local/include/jetson-inference /usr/src/jetson_multimedia_api/include)
   add_definitions(-DUSE_JETSON_INFERENCE)
endif()

# Find required packages
find_package(PkgConfig REQUIRED)

# Audio dependencies
pkg_check_modules(VORBISFILE REQUIRED vorbisfile)
include_directories(${VORBISFILE_INCLUDE_DIRS})
# Explicitly add vorbis include directories
include_directories(/usr/include/vorbis /usr/local/include/vorbis)

pkg_check_modules(ALSA REQUIRED alsa)
include_directories(${ALSA_INCLUDE_DIRS})

# Core dependencies
pkg_check_modules(JSONC REQUIRED json-c)
include_directories(${JSONC_INCLUDE_DIRS})
pkg_check_modules(SDL2 REQUIRED sdl2)
include_directories(${SDL2_INCLUDE_DIRS})
pkg_check_modules(SDL2_GFX REQUIRED SDL2_gfx)
include_directories(${SDL2_GFX_INCLUDE_DIRS})
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
include_directories(${SDL2_TTF_INCLUDE_DIRS})
pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
include_directories(${SDL2_IMAGE_INCLUDE_DIRS})
find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIRS})

# Gstreamer
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-app-1.0)
include_directories(${GSTREAMER_INCLUDE_DIRS})
pkg_check_modules(GLIB REQUIRED glib-2.0)
include_directories(${GLIB_INCLUDE_DIRS})

# Communications
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)
include_directories(${MOSQUITTO_INCLUDE_DIRS})

# OpenGL Support
set(OpenGL_GL_PREFERENCE LEGACY)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

# Misc
pkg_check_modules(GD REQUIRED gdlib)
include_directories(${GD_INCLUDE_DIRS})

# Set libraries
set(LIBRARIES
   # Audio
   ${VORBISFILE_LIBRARIES}
   ${ALSA_LIBRARIES}

   # Core
   ${JSONC_LIBRARIES}
   ${SDL2_LIBRARIES}
   ${CURL_LIBRARIES}
   ${SDL2_GFX_LIBRARIES}
   ${SDL2_TTF_LIBRARIES}
   ${SDL2_IMAGE_LIBRARIES}

   # Graphics and Video
   ${GSTREAMER_LIBRARIES}
   ${GLIB_LIBRARIES}
   ${OPENGL_LIBRARIES}
   ${GLEW_LIBRARIES}
   ${GD_LIBRARIES}
   X11
   gd

   # Communications
   ${MOSQUITTO_LIBRARIES}

   # System
   pthread
   rt
   m
   stdc++
)

# CUDA configuration
if(USE_CUDA)
   # Try to find CUDA with the version you have
   pkg_check_modules(CUDA cuda-12.6 cudart-12.6)
   if(NOT CUDA_FOUND)
      # Fallback to 12.2 if 12.6 not found
      pkg_check_modules(CUDA cuda-12.2 cudart-12.2)
   endif()
   
   if(CUDA_FOUND)
      include_directories(${CUDA_INCLUDE_DIRS})
      link_directories(${CUDA_LIBRARY_DIRS})
      list(APPEND LIBRARIES ${CUDA_LIBRARIES})
      add_definitions(-DUSE_CUDA)
      
      # Set CUDA architecture based on platform
      if(PLATFORM STREQUAL "JETSON")
         # Detect Jetson architecture
         # Nano/TX2: sm_53, Xavier: sm_72, Orin: sm_87
         # Default to sm_53 for compatibility, can be overridden
         if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
            set(CMAKE_CUDA_ARCHITECTURES "53;72;87")
         endif()
      else()
         # For desktop GPUs
         if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
            set(CMAKE_CUDA_ARCHITECTURES "60;70;75;80;86")
         endif()
      endif()
      
      message(STATUS "CUDA architectures: ${CMAKE_CUDA_ARCHITECTURES}")
   else()
      message(WARNING "CUDA packages not found, disabling CUDA support")
      set(USE_CUDA OFF)
   endif()
endif()

# Jetson Inference libraries
if(USE_JETSON_INFERENCE)
   list(APPEND LIBRARIES jetson-inference jetson-utils)
   link_directories(/usr/lib/aarch64-linux-gnu/tegra)
endif()

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

# Set CUDA properties AFTER creating the target
if(USE_CUDA)
   set_target_properties(${PROJECT_NAME} PROPERTIES
      CUDA_STANDARD 11
      CUDA_STANDARD_REQUIRED ON
      CUDA_SEPARABLE_COMPILATION ON
      CUDA_RESOLVE_DEVICE_SYMBOLS ON
   )
   
   # CUDA compile options for better performance
   target_compile_options(${PROJECT_NAME} PRIVATE 
      $<$<COMPILE_LANGUAGE:CUDA>:
         -use_fast_math
         -lineinfo
         --ptxas-options=-v
      >
   )
   
   # For mixed C/CUDA compilation
   set_property(TARGET ${PROJECT_NAME} PROPERTY CUDA_RUNTIME_LIBRARY Shared)
endif()

# Link libraries
target_link_libraries(${PROJECT_NAME} ${LIBRARIES})

# Add a custom target for indent
add_custom_target(
   indent 
   COMMAND indent -linux -nut -i3 -l100 ${SOURCE_FILES}
)

# Installation
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
