cmake_minimum_required(VERSION 3.22)

option(MOBAN_VOICE_PROBE "Build the stand-alone PCM recognition probe" OFF)
get_filename_component(moban_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${moban_root}/components" "${CMAKE_CURRENT_LIST_DIR}/moban_voice")
if(MOBAN_VOICE_PROBE)
    list(APPEND EXTRA_COMPONENT_DIRS "${moban_root}/tests/voice_probe/main")
else()
    list(APPEND EXTRA_COMPONENT_DIRS "${moban_root}/main")
    include("${moban_root}/cmake/esp_claw.cmake")
endif()

# Separate project and configuration so the regular classroom firmware keeps its footprint.
set(COMPONENTS main moban_voice)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(moban_voice_firmware)
