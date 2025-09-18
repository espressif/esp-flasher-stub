set(COMMON_SOURCES "main.cpp" "slip.cpp")

target_sources(${TARGET_NAME} PRIVATE ${COMMON_SOURCES})

if(${TARGET_CHIP} STREQUAL "esp8266")
    # zlib library will be available in esp-stub-lib. So miniz probably won't be needed

    # -Wconversion produces a warning in miniz.cpp
    get_target_property(comp_opts ${TARGET_NAME} COMPILE_OPTIONS)
    list(REMOVE_ITEM comp_opts -Wconversion)

    add_library(miniz_obj OBJECT miniz.cpp)
    target_compile_options(miniz_obj PRIVATE ${comp_opts})
    target_sources(${TARGET_NAME} PRIVATE $<TARGET_OBJECTS:miniz_obj>)
endif()
