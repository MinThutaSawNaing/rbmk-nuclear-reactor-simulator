cmake_minimum_required(VERSION 3.21)

project(RbmkControlRoom
    VERSION 1.0.0
    DESCRIPTION "RBMK-1000 Control Room Simulator"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

# ---------------------------------------------------------------- core engine
add_library(rbmk_core STATIC
    src/core/ReactorCore.cpp
)
target_include_directories(rbmk_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbmk_core PUBLIC Qt6::Core)

# --------------------------------------------------------------------- the game
qt_add_executable(RbmkControlRoom WIN32
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/CoreView.cpp
    src/ui/GaugeCard.cpp
    src/ui/TrendChart.cpp
    src/ui/Theme.cpp
)
target_link_libraries(RbmkControlRoom PRIVATE rbmk_core Qt6::Widgets Qt6::Gui)

set_target_properties(RbmkControlRoom PROPERTIES
    OUTPUT_NAME "RBMK-Control-Room"
    WIN32_EXECUTABLE TRUE
)

# Bundle the Qt runtime next to the executable:  cmake --build build --target deploy
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin")
if(WINDEPLOYQT_EXECUTABLE)
    add_custom_target(deploy
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-system-d3d-compiler
                --no-opengl-sw "$<TARGET_FILE:RbmkControlRoom>"
        DEPENDS RbmkControlRoom
        COMMENT "Deploying Qt runtime next to RBMK-Control-Room.exe")
endif()

# -------------------------------------------------------------------- unit tests
find_package(Qt6 6.5 QUIET COMPONENTS Test)
if(TARGET Qt6::Test)
    enable_testing()
    qt_add_executable(rbmk_tests tests/core_tests.cpp)
    target_link_libraries(rbmk_tests PRIVATE rbmk_core Qt6::Test)
    add_test(NAME core_physics COMMAND rbmk_tests)
endif()
