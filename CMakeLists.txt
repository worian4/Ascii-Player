cmake_minimum_required(VERSION 3.10)
project(ASCII_Player LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    main.cpp
    ascii_render.cpp
)

set(HEADERS
    ascii_render.hpp
)

if (WIN32)
    list(APPEND SOURCES icon.rc)
endif()

# OpenCV
if (WIN32)
    set(OpenCV_DIR "C:/opencv/build/x64/vc16/lib")
endif()
find_package(OpenCV REQUIRED)

if (UNIX AND NOT WIN32)
    find_package(Threads REQUIRED)
endif()

add_executable(ASCII_Player ${SOURCES} ${HEADERS})

# include dirs
target_include_directories(ASCII_Player PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)

if (WIN32)
    target_include_directories(ASCII_Player PRIVATE
        ${CUDA_INCLUDE_DIR}
        ${FFMPEG_INCLUDE_DIR}
    )
endif()

if (WIN32)
    target_link_directories(ASCII_Player PRIVATE
        ${CUDA_LIB_DIR}
    )
endif()

# link libs
if (WIN32)
    target_link_libraries(ASCII_Player PRIVATE
        ${OpenCV_LIBS}
    )
else()
    target_link_libraries(ASCII_Player PRIVATE
        ${OpenCV_LIBS}
        dl
        ncurses
        Threads::Threads
    )
endif()

if (WIN32)
    add_custom_command(TARGET ASCII_Player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/libvlc.dll"
            "${CMAKE_SOURCE_DIR}/libvlccore.dll"
            $<TARGET_FILE_DIR:ASCII_Player>
    )
    add_custom_command(TARGET ASCII_Player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/plugins"
            "$<TARGET_FILE_DIR:ASCII_Player>/plugins"
    )
endif()
