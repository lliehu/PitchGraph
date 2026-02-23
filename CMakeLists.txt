cmake_minimum_required(VERSION 3.16)
project(PitchGraph)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_executable(PitchGraph
    main.cpp
    MainWindow.cpp
    MainWindow.h
)

target_link_libraries(PitchGraph
    Qt6::Core
    Qt6::Widgets
)
