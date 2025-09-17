cmake_minimum_required(VERSION 3.16)
project(yt_dlp_gui_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(yt-dlp-gui
    src/main.cpp
    src/MainWindow.cpp
)

target_include_directories(yt-dlp-gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(yt-dlp-gui PRIVATE Qt6::Widgets Qt6::Network)
