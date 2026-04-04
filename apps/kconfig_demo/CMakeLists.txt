# Set the minimum required CMake version
cmake_minimum_required(VERSION 3.5)

# Include the ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Add external components to the project
list(APPEND EXTRA_COMPONENT_DIRS ../../components)

# Set the project name
project(app)