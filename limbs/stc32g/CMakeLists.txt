# Host consumers link the same portable node and sensor drivers as the R01 project.
add_library(moban_limb STATIC temperature_node.c node_target.c tip_acquisition.c aht20.c vl53l1x.c)
target_include_directories(moban_limb PUBLIC . ../../components/limb_protocol/include)
set_target_properties(moban_limb PROPERTIES POSITION_INDEPENDENT_CODE ON)
