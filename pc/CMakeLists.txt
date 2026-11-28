cmake_minimum_required(VERSION 3.22)
project(moban_bench C)
if(MSVC)
    add_compile_options(/utf-8)
endif()
add_subdirectory(../limbs/stc32g limb)
add_library(moban_core SHARED bench_limb.c vl53l1x_fixture.c ../main/slot_observation.c ../main/limb_scan.c
    ../components/moban_core/moban_core.c ../components/limb_reader/limb_reader.c)
target_link_libraries(moban_core PRIVATE moban_limb)
target_include_directories(moban_core PRIVATE ../main ../components/moban_core/include
    ../components/limb_reader/include)
target_compile_features(moban_core PRIVATE c_std_11)
set_target_properties(moban_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/library-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:moban_core>")
