cmake_minimum_required(VERSION 3.20)
project(openwarband LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- raylib (fetched automatically; works on Windows, Linux, macOS) ---
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(openwarband
    src/main.cpp
    src/campaign.cpp
    src/battle.cpp
)
target_link_libraries(openwarband PRIVATE raylib)

# macOS needs these frameworks
if(APPLE)
    target_link_libraries(openwarband PRIVATE
        "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

# Windows: hide the console window in Release builds
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(openwarband PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
