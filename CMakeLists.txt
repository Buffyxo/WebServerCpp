cmake_minimum_required(VERSION 3.10)

project(WebServer LANGUAGES CXX)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directory
include_directories(include)

# Glob all source files from src/
file(GLOB SOURCES "src/*.cpp")

# Create executable
add_executable(web_server ${SOURCES})

# Add /web
file(COPY ${CMAKE_SOURCE_DIR}/web DESTINATION ${CMAKE_BINARY_DIR})

# Link Ws2_32 on Windows
if(WIN32)
    target_link_libraries(web_server PRIVATE Ws2_32)
endif()

# Set output directory
set_target_properties(web_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../build
)