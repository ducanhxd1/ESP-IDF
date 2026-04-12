# Required CMake version
cmake_minimum_required(VERSION 3.5)

set (EXTRA_COMPONENT_DIRS C:/Users/DucAnh/Desktop/workspace_esp32idf/components)

# Set up ESP-IDF build enviroment
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set the project name
project(app)
