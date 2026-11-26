include(FetchContent)
FetchContent_Declare(esp_claw
    GIT_REPOSITORY https://github.com/espressif/esp-claw.git
    GIT_TAG 50072775cfa20d7ce25a9b27591d9d7c8b9dbab5
    GIT_SUBMODULES "")
FetchContent_MakeAvailable(esp_claw)
list(APPEND EXTRA_COMPONENT_DIRS
    "${esp_claw_SOURCE_DIR}/components/claw_modules"
    "${esp_claw_SOURCE_DIR}/components/common/http_reuse")
