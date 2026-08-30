cmake_minimum_required(VERSION 3.16)
project(AgentFlightRecorder LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

# C recorder — no dependencies beyond libc and pthread.
add_executable(recorder recorder/recorder.c)
find_package(Threads REQUIRED)
target_link_libraries(recorder PRIVATE Threads::Threads)

# Qt UI — stock widgets + Charts.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Charts)
add_executable(flight_ui
    ui/main.cpp
    ui/mainwindow.cpp
    ui/mainwindow.h
)
target_link_libraries(flight_ui PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Charts)
