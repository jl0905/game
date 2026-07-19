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
    src/harness.cpp
    src/ui.cpp
    src/content.cpp
    src/campaign/campaign.cpp
    src/battle/battle.cpp
    src/battle/character.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(openwarband PRIVATE raylib Threads::Threads)

# Ship the runtime assets (fonts, config) next to the executable so the game
# finds them via GetApplicationDirectory() regardless of the working directory.
add_custom_command(TARGET openwarband POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:openwarband>/assets
    COMMENT "Copying assets to build output")

# macOS needs these frameworks
if(APPLE)
    target_link_libraries(openwarband PRIVATE
        "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

# Windows: hide the console window in Release builds
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(openwarband PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
