cmake_minimum_required(VERSION 3.10)
project(ASCII_Player)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    icon.rc
    main.cpp
    ascii_render.cpp
)

set(HEADERS
    ascii_render.hpp
)

# OpenCV
set(OpenCV_DIR "C:/opencv/build/x64/vc16/lib")
find_package(OpenCV REQUIRED)


add_executable(ASCII_Player ${SOURCES} ${HEADERS})

target_include_directories(ASCII_Player PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CUDA_INCLUDE_DIR}
    ${FFMPEG_INCLUDE_DIR}
)

target_link_directories(ASCII_Player PRIVATE
    ${CUDA_LIB_DIR}
)

target_link_libraries(ASCII_Player PRIVATE
    ${OpenCV_LIBS}
)

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
