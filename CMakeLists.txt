cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH})
    set(ENV{IDF_PATH} "${CMAKE_CURRENT_LIST_DIR}/esp-idf")
endif()

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/Drivers
    ${CMAKE_CURRENT_LIST_DIR}/Application/Network
    ${CMAKE_CURRENT_LIST_DIR}/Application/Hub75
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_matrix_led)
