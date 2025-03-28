# Incluir o módulo FetchContent do CMake
include(FetchContent) 

FetchContent_Declare(
    rcheevos
    GIT_REPOSITORY https://github.com/RetroAchievements/rcheevos.git
    GIT_TAG        master
)

FetchContent_MakeAvailable(rcheevos)

include(files.cmake)
add_executable(${NAME}
    main.c
    ${SRC_FILES}
    ${rcheevos_SOURCE_DIR}/src/rhash/md5.c
    ${rcheevos_SOURCE_DIR}/src/rc_util.c
    ${rcheevos_SOURCE_DIR}/src/rc_client.c
    ${rcheevos_SOURCE_DIR}/src/rc_compat.c
    ${rcheevos_SOURCE_DIR}/src/rapi/rc_api_runtime.c
    ${rcheevos_SOURCE_DIR}/src/rapi/rc_api_common.c
    ${rcheevos_SOURCE_DIR}/src/rapi/rc_api_info.c
    ${rcheevos_SOURCE_DIR}/src/rapi/rc_api_user.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/trigger.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/alloc.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/condition.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/condset.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/consoleinfo.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/format.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/lboard.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/memref.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/operand.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/rc_validate.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/richpresence.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/runtime_progress.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/runtime.c
    ${rcheevos_SOURCE_DIR}/src/rcheevos/value.c
)

# Create C header file with the name <pio program>.pio.h
pico_generate_pio_header(${NAME} ${CMAKE_CURRENT_LIST_DIR}/memory-bus.pio)

target_include_directories(${NAME} PRIVATE
    ${rcheevos_SOURCE_DIR}/include
    ${rcheevos_SOURCE_DIR}/src
    ${rcheevos_SOURCE_DIR}/src/rcheevos
)

add_compile_definitions(RC_DISABLE_LUA=1)
add_compile_definitions(RC_NO_THREADS=1)

# Pull in our pico_stdlib which pulls in commonly used features
target_link_libraries(${NAME} pico_stdlib pico_stdlib hardware_pio pico_multicore hardware_dma) 

# enable usb output, disable uart output
pico_enable_stdio_usb(${NAME} 1)
pico_enable_stdio_uart(${NAME} 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(${NAME})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")