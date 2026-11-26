cmake_minimum_required(VERSION 3.22)
project(moban_bench C)
add_library(moban_core SHARED bench_limb.c ../main/slot_observation.c
    ../components/moban_core/moban_core.c ../components/limb_reader/limb_reader.c
    ../limbs/stc32g/tip_acquisition.c ../limbs/stc32g/temperature_node.c)
target_include_directories(moban_core PRIVATE ../main ../limbs/stc32g ../components/moban_core/include
    ../components/limb_protocol/include ../components/limb_reader/include)
target_compile_features(moban_core PRIVATE c_std_11)
set_target_properties(moban_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/library-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:moban_core>")
