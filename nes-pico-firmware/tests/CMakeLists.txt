set(TEST_NAME "unit_tests")
project(${TEST_NAME} C CXX ASM) 
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Define o compilador C e C++ (MinGW GCC)
set(CMAKE_C_COMPILER "C:/VSARM/mingw/mingw32/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "C:/VSARM/mingw/mingw32/bin/g++.exe")


# Incluir o módulo FetchContent do CMake
include(FetchContent) 

# Baixar o Unity do GitHub
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        master
)

FetchContent_MakeAvailable(unity)

FetchContent_Declare(
    rcheevos
    GIT_REPOSITORY https://github.com/odelot/rcheevos
    GIT_TAG        v11.6.0
)

FetchContent_MakeAvailable(rcheevos)

include(../src/files.cmake)

add_executable(${TEST_NAME}
    test_main.c
    test_rcheevos.c
    test_search.c
	${SRC_FILES} 
    ${unity_SOURCE_DIR}/src/unity.c
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

add_compile_definitions(RC_DISABLE_LUA=1)
add_compile_definitions(RC_NO_THREADS=1)
add_compile_definitions(DEBUG_BUFFERS=1)


# Diretório onde estão as bibliotecas adicionais
link_directories(
    ${CMAKE_SOURCE_DIR}/lib
    C:/VSARM/mingw/mingw32/lib
)

target_include_directories(${TEST_NAME} PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../src
    ${unity_SOURCE_DIR}/src
    ${rcheevos_SOURCE_DIR}/include
    ${rcheevos_SOURCE_DIR}/src
    ${rcheevos_SOURCE_DIR}/src/rcheevos
    ${rcheevos_SOURCE_DIR}/src/rapi
)

target_link_libraries(${TEST_NAME} winpthread)

