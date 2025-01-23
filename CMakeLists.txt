cmake_minimum_required(VERSION 3.12.1)

# (More or less a copy of https://github.com/jpvanoosten/LearningDirectX12/blob/v0.0.1/CMakeLists.txt)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY $(CMAKE_BINARY_DIR)/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY $(CMAKE_BINARY_DIR)/binary)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $(CMAKE_BINARY_DIR)/binary)

project(Dx12 
LANGUAGES CXX)