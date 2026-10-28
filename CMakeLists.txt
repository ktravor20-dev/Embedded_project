cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "src"
    "files_to_work_in"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(Embedded_Project)