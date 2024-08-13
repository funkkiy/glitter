cmake_minimum_required(VERSION 3.15)

project(Glitter LANGUAGES CXX C)

string(CONCAT WALL_OTHERS "$<"
    "$<AND:"
        "$<COMPILE_LANGUAGE:CXX,C>,"
        "$<NOT:"
            "$<CXX_COMPILER_ID:MSVC>"
        ">"
    ">:"
    "SHELL:-Wall -Wextra -Wdouble-promotion -Wshadow"
">")

string(CONCAT WALL_MSVC "$<"
    "$<AND:"
        "$<COMPILE_LANGUAGE:CXX,C>,"
        "$<CXX_COMPILER_ID:MSVC>"
    ">:"
    "SHELL:/W4 /wd4201"
">")

# multi-cpu build
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP>)

# colored output
set(CMAKE_COLOR_DIAGNOSTICS TRUE)

#[[
string(CONCAT ASAN_MSVC "$<"
    "$<AND:"
        "$<COMPILE_LANGUAGE:CXX,C>,"
        "$<CXX_COMPILER_ID:MSVC>,"
        "$<CONFIG:DEBUG>"
    ">:"
    "SHELL:/fsanitize=address"
">")
add_compile_options(${ASAN_MSVC})
]]

# enable folder hierarchy: filters on msvc
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# output to current binary directory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# glfw
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/glfw)

# spdlog
set(SPDLOG_ENABLE_PCH ON CACHE BOOL "" FORCE)
add_subdirectory(vendor/spdlog)
# disable upstream spdlog warnings
set_property(TARGET spdlog PROPERTY SYSTEM TRUE)

# glm
add_subdirectory(vendor/glm)

# place all the dependencies in a single folder
set_target_properties(glfw PROPERTIES FOLDER "Dependencies/GLFW3")
set_target_properties(update_mappings PROPERTIES FOLDER "Dependencies/GLFW3")
set_target_properties(spdlog PROPERTIES FOLDER "Dependencies")
set_target_properties(glm PROPERTIES FOLDER "Dependencies")

# glitter target
list(APPEND GLITTER_SOURCES
    # glitter
    src/main.cpp

    # glitter utility
    src/glitter/util/File.cpp
    src/glitter/util/File.h
	
	# glad
    vendor/glad/src/glad.c
	
	# stb
    vendor/stb/stb_image.cpp

    # cgltf
    vendor/cgltf/cgltf.cpp
)

list(APPEND GLITTER_INCLUDES
    # glitter
    src/glitter
	
	# glad
    vendor/glad/include
	
	# stb
    vendor/stb

    # cgltf
    vendor/cgltf
)

add_executable(Glitter)
target_sources(Glitter PRIVATE
    ${GLITTER_SOURCES}
)
target_include_directories(Glitter PRIVATE
    ${GLITTER_INCLUDES}
)
target_link_libraries(Glitter glfw spdlog glm)
target_compile_features(Glitter PRIVATE cxx_std_23)
target_compile_options(Glitter PRIVATE
    ${WALL_OTHERS} ${WALL_MSVC}
)
target_compile_definitions(Glitter PUBLIC SPDLOG_COMPILED_LIB)

# msvc-specific Glitter settings
if(MSVC)
    set_target_properties(Glitter PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data
    )
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PROPERTY VS_STARTUP_PROJECT Glitter
    )
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${GLITTER_SOURCES})
endif()
