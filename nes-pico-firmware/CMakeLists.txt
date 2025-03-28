# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.12)
if(PICO_BUILD)
    set(PICO_BOARD pico)
    set(NAME nes-pico-firmware)
    # Include build functions from Pico SDK
    include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    include($ENV{PICO_SDK_PATH}/tools/CMakeLists.txt)

    # Set name of project (as PROJECT_NAME) and C/C++ standards
    project(${NAME} C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_BUILD_TYPE Release)

    # Creates a pico-sdk subdirectory in our project for the libraries
    pico_sdk_init()

    add_subdirectory(src)
endif()

if(UNIT_TESTS_BUILD)

    add_subdirectory(tests)

    enable_testing()
    add_test(NAME tests COMMAND unit_tests)
endif()

