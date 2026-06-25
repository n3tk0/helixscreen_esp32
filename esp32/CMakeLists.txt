# SPDX-License-Identifier: GPL-3.0-or-later
#
# HelixScreen ESP32-S3 firmware — top-level ESP-IDF project file.
#
# This is a SEPARATE build from the desktop/embedded-Linux HelixScreen app in
# the repo root. It does NOT use the root Makefile. Build it with ESP-IDF:
#
#   cd esp32
#   idf.py set-target esp32s3
#   idf.py menuconfig      # set WiFi creds + Moonraker host (Helix Screen menu)
#   idf.py build flash monitor
#
# Target board: Waveshare ESP32-S3-Touch-LCD-2.8 (ST7789 320x240 + cap touch).

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(helix-screen-esp32)
