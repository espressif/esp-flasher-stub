set(COMMON_SOURCES "main.c" "slip.c")

target_sources(${TARGET_NAME} PRIVATE ${COMMON_SOURCES})

if(${TARGET_CHIP} STREQUAL "esp8266")
    # zlib library will be available in esp-stub-lib, so miniz probably won't be needed.
    add_library(miniz_obj OBJECT miniz.c)

    target_compile_options(miniz_obj PRIVATE
        # Disable specific warnings that miniz cannot satisfy
        -Wno-conversion         # Disable implicit conversion warnings
        -Wno-sign-conversion    # Disable signed/unsigned conversion warnings
    )

    target_sources(${TARGET_NAME} PRIVATE $<TARGET_OBJECTS:miniz_obj>)
    message(STATUS "Including miniz with custom safe flags for ${TARGET_CHIP}")
endif()
