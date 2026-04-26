cmake_minimum_required(VERSION 3.16)
project(PitchGraph)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(PITCHGRAPH_SOURCES
    main.cpp
    MainWindow.cpp
    MainWindow.h
    PitchDetector.cpp
    PitchDetector.h
    AudioCapture.cpp
    AudioCapture.h
    AudioCaptureFactory.cpp
    AudioCaptureFactory.h
    PitchGraphWidget.cpp
    PitchGraphWidget.h
)

if(WIN32)
    list(APPEND PITCHGRAPH_SOURCES
        WasapiAudioCapture.cpp
        WasapiAudioCapture.h
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND PITCHGRAPH_SOURCES
        PulseAudioCapture.cpp
        PulseAudioCapture.h
    )
endif()

if(WIN32)
    if(DEFINED AUBIO_ROOT)
        find_path(AUBIO_INCLUDE_DIR
            NAMES aubio/aubio.h
            PATHS "${AUBIO_ROOT}/include"
            NO_DEFAULT_PATH
        )
        find_library(AUBIO_LIBRARY
            NAMES aubio-5 aubio libaubio-5 libaubio
            PATHS "${AUBIO_ROOT}/lib"
            NO_DEFAULT_PATH
        )
    endif()

    if(NOT AUBIO_INCLUDE_DIR)
        find_path(AUBIO_INCLUDE_DIR NAMES aubio/aubio.h)
    endif()
    if(NOT AUBIO_LIBRARY)
        find_library(AUBIO_LIBRARY NAMES aubio-5 aubio libaubio-5 libaubio)
    endif()

    if(NOT AUBIO_INCLUDE_DIR OR NOT AUBIO_LIBRARY)
        message(FATAL_ERROR "aubio was not found. Set -DAUBIO_ROOT=<path-to-aubio> (e.g. K:/sw/aubio-0.4.6-win64).")
    endif()

    set(AUBIO_INCLUDE_DIRS "${AUBIO_INCLUDE_DIR}")
    set(AUBIO_LIBRARIES "${AUBIO_LIBRARY}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AUBIO REQUIRED aubio)
    pkg_check_modules(PULSEAUDIO REQUIRED libpulse libpulse-simple)
else()
    message(FATAL_ERROR "Unsupported platform. Only Windows and Linux are configured.")
endif()

add_executable(PitchGraph ${PITCHGRAPH_SOURCES})

target_include_directories(PitchGraph PRIVATE
    ${AUBIO_INCLUDE_DIRS}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_include_directories(PitchGraph PRIVATE
        ${PULSEAUDIO_INCLUDE_DIRS}
    )
endif()

target_link_libraries(PitchGraph PRIVATE
    Qt6::Core
    Qt6::Widgets
    ${AUBIO_LIBRARIES}
)

if(WIN32)
    target_compile_definitions(PitchGraph PRIVATE _WIN32_WINNT=0x0601)
    target_link_libraries(PitchGraph PRIVATE
        ole32
        uuid
        avrt
        ksuser
    )

    if(DEFINED AUBIO_ROOT)
        set(AUBIO_RUNTIME_DLL "${AUBIO_ROOT}/bin/libaubio-5.dll")
    else()
        set(AUBIO_RUNTIME_DLL "K:/sw/aubio-0.4.6-win64/bin/libaubio-5.dll")
    endif()

    if(EXISTS "${AUBIO_RUNTIME_DLL}")
        add_custom_command(TARGET PitchGraph POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${AUBIO_RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:PitchGraph>"
            VERBATIM
        )
    else()
        message(WARNING "Aubio runtime DLL not found at ${AUBIO_RUNTIME_DLL}. Ensure it is available on PATH when running.")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(PitchGraph PRIVATE
        ${PULSEAUDIO_LIBRARIES}
    )
endif()
