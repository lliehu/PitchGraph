cmake_minimum_required(VERSION 3.16)
project(PitchGraph)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AUBIO REQUIRED aubio)
pkg_check_modules(PULSEAUDIO REQUIRED libpulse libpulse-simple)

add_executable(PitchGraph
    main.cpp
    MainWindow.cpp
    MainWindow.h
    PitchDetector.cpp
    PitchDetector.h
    AudioCapture.cpp
    AudioCapture.h
    PitchGraphWidget.cpp
    PitchGraphWidget.h
)

target_include_directories(PitchGraph PRIVATE
    ${AUBIO_INCLUDE_DIRS}
    ${PULSEAUDIO_INCLUDE_DIRS}
)

target_link_libraries(PitchGraph
    Qt6::Core
    Qt6::Widgets
    ${AUBIO_LIBRARIES}
    ${PULSEAUDIO_LIBRARIES}
)
